# 里程碑 13：完整虚拟地址空间、按需分页与 COW

日期：2026-11-06

## 1. 目标

里程碑 12 结束了“用户进程一次性占用 4MB 连续物理块”的设计。每个
进程虽然已经有独立页目录，但页表预先填满 1024 个用户页，启动即消耗
约 4MB，fork 也要复制固定 16KB 镜像区和 64KB 栈区。

里程碑 13 把这条路径升级为：

```text
独立页目录/页表
  → 只装入程序镜像实际占用的页
  → 栈、堆、mmap 首次访问时补零页
  → fork 先共享物理页，写入时才复制 (COW)
  → 栈底 guard page 拦截向下越界
  → brk 管理堆边界，mmap 管理匿名映射窗口
```

完成后，`run hello` 的地址空间只映射 1 个代码页；用户栈也在第一次
函数调用写栈时通过 #PF 才获得物理页。

## 2. 用户虚拟地址布局

用户虚拟窗口仍然是 `0x02000000..0x02400000`，但中间区域不再预映射：

```text
0x02000000  flat 镜像 / 代码 / 数据
0x02040000  堆基址 (brk 从这里开始)
0x020C0000  匿名 mmap 窗口
0x022F0000  guard page
0x022F1000  栈底
0x02400000  栈顶 (第一个字节不可访问)
```

各区间用途：

| 区域 | 范围 | 分配策略 |
|---|---|---|
| 镜像 | `IMAGE_BASE..IMAGE_LIMIT` | spawn 时按实际镜像页装入 |
| 堆 | `HEAP_BASE..HEAP_LIMIT` | `brk` 扩大后，首次写访问补零页 |
| mmap | `MMAP_BASE..MMAP_LIMIT` | 预留地址，首次访问补零页 |
| guard | `STACK_GUARD..STACK_BOTTOM` | 永不映射 |
| 栈 | `STACK_BOTTOM..STACK_TOP` | 向下使用时逐页补零页 |

用户进程的物理页来自 E820 managed 分配器，可以位于 1MB 到 32MB 的
任意空闲位置；虚拟地址固定不代表物理地址连续。

## 3. x86 两级页表

32 位虚拟地址被拆成：

```text
31..22  PDE 下标
21..12  PTE 下标
11..0   页内偏移
```

地址转换：

```text
CR3
 └─ 页目录 → PDE
              └─ 页表 → PTE
                          └─ 物理页 + 偏移
```

内核页目录继续恒等映射物理 `0..32MB`，因此分配器返回的物理页可直接
通过同值指针访问。用户页目录复制内核 PDE 0..7，再把自己的页表挂在
PDE 8 开始的虚拟窗口下。

PDE 与 PTE 都带 `U/S=1` 后，Ring 3 才能访问；内核 PDE 0..7 不带用户
位，所以用户程序直接访问内核地址仍会触发 #PF。

## 4. 物理页引用计数

COW 需要让父子页表暂时指向同一物理页，因此 `memory.c` 增加：

```c
page_get(phys);       // +1
page_put(phys);       // -1，归零时才真正释放
page_refcount(phys);
get_shared_pages();
```

页分配成功后引用数为 1。fork 为每个共享页调用 `page_get`，进程销毁时
对每个映射调用 `page_put`。页表页和页目录页仍由创建它们的地址空间
独占，直接 `free_page`。

## 5. 创建与销毁地址空间

`paging_create_user_dir()` 只分配一个页目录：

1. 清零整个页目录。
2. 复制内核 PDE 0..7，保留中断/系统调用时可直接执行内核代码。
3. 保留草稿窗口 PDE。
4. 用户 PDE 8..29 暂时为空。

`vm_init_image()` 随后只对实际镜像大小做页对齐：

```text
镜像字节数 → 向上取整页数
每页: alloc_page → 清零 → copy_from_embedded → map_page
```

`vm_destroy()` 遍历用户 PDE：

```text
每个 present PTE → page_put
释放页表页
释放页目录页
```

收尸时不能仍使用目标页目录作为 CR3，因此现有 `proc_wait` 和孤儿清扫
顺序保持不变：先把任务标成 ZOMBIE 并切走，再销毁它的地址空间。

## 6. #PF 按需补页

`exc_dispatch()` 在打印异常现场前先调用：

```c
vm_handle_page_fault(task_current(), cr2, error)
```

错误码关键位：

- `bit 0 = P`：0 表示页不存在，1 表示页存在但权限不允许。
- `bit 1 = W`：1 表示写访问。
- `bit 2 = U`：1 表示用户态访问。

对于“页不存在 + 用户访问”，VM 层根据 CR2 判定故障是否属于合法范围：

```text
地址在本次 brk 覆盖范围   → 分配零页
地址在已 mmap 覆盖范围    → 分配零页
地址在用户栈范围          → 分配零页
其他地址                 → 拒绝并杀进程
```

补页成功后 #PF 处理器直接返回异常桩，`iret` 重新执行触发故障的指令。
串口输出例如：

```text
[VM] demand pid=0x00000005 area=stack addr=0x023FF000
[VM] demand pid=0x00000005 area=heap addr=0x02040000
[VM] demand pid=0x00000005 area=mmap addr=0x020C0000
```

## 7. fork 与写时复制

`proc_fork()` 不再复制用户物理页。`vm_clone()`：

1. 为新进程创建空页目录。
2. 父进程每个存在的 PDE 都分配一个新页表。
3. 复制 PTE，但清掉 `PAGE_WRITE` 并设置软件位 `PAGE_COW`。
4. 对共享物理页调用 `page_get`。
5. 整份页表成功后，冻结父进程对应 PTE。
6. 重载父 CR3 清掉 TLB 中仍可写的旧项。

COW 写故障组合通常是：

```text
P=1, W=1, U=1
```

处理逻辑：

```text
引用数 > 1
  → 分配新页
  → 复制旧页 4096 字节
  → PTE 指向新页并恢复 PAGE_WRITE
  → page_put(旧页)

引用数 == 1
  → 对方已经退出，无需复制
  → 直接恢复 PAGE_WRITE
```

最后执行 `invlpg`，只失效当前故障页。

## 8. Guard page

栈底下方保留一个永久不存在的页。栈向低地址增长越过合法范围时，CPU
产生 #PF，VM 层识别出 guard 范围：

```text
[VM] reject pid=0x00000007 addr=0x022F0000 stack guard page
```

随后异常处理打印 CR2/EIP 并杀进程，Shell 继续运行。`vmguard` 演示程序
专门执行一次 guard 写入，作为自动化回归入口。

## 9. brk 与 mmap

### brk

`SYS_BRK` 接收新断点，返回旧断点：

```c
void* brk(void* new_break);
```

- 新断点小于堆基址或超过堆上限：返回 `(void*)-1`。
- 扩大：只更新边界，不立即分配物理页。
- 收缩：对不再覆盖的已映射页执行 unmap、`page_put` 和 `invlpg`。

### mmap

`SYS_MMAP` 是教学版匿名 mmap：

```c
void* mmap(uint32_t length);
```

长度按页对齐，从 mmap 窗口 bump 分配虚拟地址；物理页仍由首次访问
补页。当前没有 `munmap`，地址空间销毁时统一释放。

## 10. 系统调用指针安全

按需分页后，一个地址落在“用户虚拟窗口”内并不代表页已经映射。若内核
直接解引用，会从 Ring 0 触发 #PF。

`sys_puts()` 因此通过 `vm_user_range_ok()` 逐页调用
`paging_translate()` 检查。字符串跨入未映射页时返回错误，而不是把内核
带入异常。

循环条件也改为：

```c
while (s < region_end && *s)
```

这样不会在边界处先解引用、后判断指针是否仍合法。

## 11. 踩坑记录

1. **任务过早标 READY**：创建进程时先分配页目录、再逐页装镜像。如果
   在地址空间完成前就标 READY，定时器可能切进半初始化任务。修法：
   初始化期间保持 IDLE，帧、CR3 和页表全部完成后才改 READY。
2. **COW 必须等子页表完整后再冻结父页**：否则后半段分配失败时父进程
   已经变成只读，需要复杂回滚。实现先完整复制子页表，再统一修改父 PTE。
3. **修改只读 PTE 后要刷 TLB**：fork 返回后原页表项在 TLB 中仍可写，
   不刷新会绕过 COW。父 CR3 重载，COW 单页用 `invlpg`。
4. **brk 收缩后不能继续访问旧堆**：首次 vmtest 在收缩后读取
   `heap[0]`，按设计触发 #PF 并被杀。测试应在收缩前保存所需值，再验证
   边界已撤销。
5. **启动日志兼容**：Ring 3 回归依赖
   `[USER] user region 0x02000000..0x02400000` 标记，改为新架构时需保留
   旧断言文本，避免把日志改名误判成功能回归。

## 12. 运行与观察

```bash
make
make run
```

进入 Shell 后：

```text
OS:/> meminfo
OS:/> run vmtest
OS:/> run vmguard
OS:/> ps
```

`vmtest` 会依次触发栈、堆和 mmap 补页，然后 fork、写入共享页并断言
父子值互不影响。`vmguard` 故意写栈 guard page，预期显示 #14 后回到
Shell。

## 13. 自动化验证

新增：

```bash
python3 tools/test-vm.py
```

共 22 项，覆盖：

- 每进程 VM 启动标记与镜像实际页映射。
- 栈、堆、mmap 的按需补页。
- brk 初始边界、扩大与收缩。
- fork 页共享和引用计数。
- 父子 COW 写入、私有值隔离。
- guard page 拒绝、#PF 现场和异常杀进程。
- Shell 存活、孤儿僵尸回收。
- 所有前台进程退出后 managed page 数回到基线。

已回归：

- `test-user.py`：36 项。
- `test-proc.py`：36 项。
- 既有测试套件与新 VM 套件合计 321 项。

全部测试入口：

```bash
make test-all
```

## 14. 当前限制与下一步

- 镜像格式仍是 flat binary；BSS 边界依赖链接产物，尚未解析 ELF
  program header。
- mmap 只有匿名映射，没有 `munmap`、文件映射和权限参数。
- COW 在页粒度复制；没有 reverse mapping，也没有按需零页的全局共享。
- 物理恒等映射仍限制为 32MB；E820 中更高物理内存暂不参与分配。

自然下一步是：

```text
ELF loader / exec
  → 文件映射 mmap 与 munmap
  → 用户态 shell
  → 管道与重定向
```
