# 里程碑 12：E820 内存地图与内核堆

这一章完成三个目标：

1. 从 BIOS 获取真实的物理内存地图，而不是假定内存连续。
2. 让物理页分配器只分配 `usable` 区域，并保护内核、页表和保留区。
3. 建立内核堆 `kmalloc/kfree/krealloc`，通过 `memmap`、`heap`、
   `heaptest` 在系统内观察和验证。

完成后的学习链路：

```text
BIOS E820
  → Bootloader 保存内存地图
  → 内核复制并解析
  → 物理页位图
  → reserved 位图
  → 连续页分配
  → 内核堆块
  → kmalloc / kfree / krealloc
  → Shell 观察与压力测试
```

## 建议的学习方式

每一步都按下面顺序进行，不要只看代码：

1. 先阅读本节的概念和目标。
2. 打开对应源文件，看数据结构和函数入口。
3. 执行 `make`，确认项目能构建。
4. 执行 `make test-memory`，观察 15 项测试结果。
5. QEMU 启动后亲自输入 `memmap`、`meminfo`、`heap`、`heaptest`。
6. 完成文末至少两个实验，并记录输出与结论。
7. 用 GDB 查看一个物理地址、一个堆块和一个 E820 结构。
8. 最后阅读“常见错误”，确认自己能解释每个修复原因。

## 1. 为什么原来的内存管理不够

里程碑 11 中使用：

```c
uint16_t ext_mem_kb = *(uint16_t*)0x500;
uint32_t total_mem = 1024 * 1024 + ext_mem_kb * 1024;
```

这只能得到一个近似总容量，无法知道：

- 哪些地址真的可用。
- 哪些范围属于 BIOS、ACPI、显存或设备。
- 可用内存中间是否存在空洞。
- 哪一段属于内核和页表。
- 用户错误释放会不会破坏内核资源。

真实操作系统必须根据固件提供的内存地图管理物理页。

## 2. E820 是什么

BIOS `int 0x15` 的 `EAX=0xE820` 服务返回一段段物理内存范围：

| 偏移 | 大小 | 字段 |
|---|---:|---|
| 0 | 8 | 起始物理地址 `base` |
| 8 | 8 | 长度 `length` |
| 16 | 4 | 类型 `type` |
| 20 | 4 | ACPI 扩展属性 |

常见类型：

| type | 含义 | NewOS 是否分配 |
|---:|---|---|
| 1 | usable | 是 |
| 2 | reserved | 否 |
| 3 | ACPI reclaimable | 否，后续可研究 |
| 4 | ACPI NVS | 否 |
| 5 | bad memory | 否 |

调用时需要：

- `EDX = 0x534D4150`，即 ASCII `SMAP`。
- `ECX = 24`，缓冲区大小。
- `ES:DI` 指向输出缓冲区。
- 每次返回的 `EBX` 作为下一次调用的继续值。
- `EBX` 返回 0 或 `CF=1` 时结束。

## 3. 启动阶段的低内存布局

NewOS 当前低内存约定：

| 地址 | 用途 |
|---|---|
| `0x0500` | 旧 BIOS 扩展内存大小 |
| `0x0580` | BIOS GDTR |
| `0x0586` | BIOS IDTR |
| `0x0600` | 启动驱动器号 |
| `0x2000` | E820 表，最多 32 项 |
| `0x4000` | E820 项数 |
| `0x5400` | 实模式往返保存区 |
| `0x6000` | 实模式跳板 |
| `0x7000` | SeaBIOS 实际运行栈附近，禁止占用 |
| `0x7C00` | Boot Sector |
| `0x10000` | 内核链接地址 |

E820 表选择 `0x2000`，不能放在 `0x7000`，因为部分 BIOS 会在实模式
调用期间把栈放在那里。

## 4. 在 Bootloader 中读取 E820

入口先保存 BIOS 传入的启动驱动器：

```asm
mov [0x600], dl
```

这一步必须在 `int 0x15` 之前。BIOS 中断不保证 `DL` 在返回后仍然保持
启动驱动器号。

清空 E820 缓冲：

```asm
xor edi, edi
mov di, 0x2000
xor eax, eax
mov ecx, 32 * 6
rep stosd
```

注意：在 16 位实模式中，`mov di, 0x2000` 不会清零 EDI 的高 16 位，而
`rep stosd` 使用完整 EDI。因此必须先 `xor edi, edi`。

读取循环：

```asm
mov di, 0x2000
xor ebx, ebx
xor bp, bp
.e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    inc bp
    add di, 24
    test ebx, ebx
    jnz .e820_loop
```

最后保存项数：

```asm
mov [0x4000], bp
```

随后恢复段寄存器，再进入磁盘读取：

```asm
xor ax, ax
mov ds, ax
mov es, ax
```

否则 BIOS 调用遗留的 DS/ES 可能让后续 `int 13h` 使用错误的启动驱动器号
或数据段。

## 5. 内核如何读取 E820 结果

内核结构必须使用 packed，避免编译器插入对齐填充：

```c
typedef struct __attribute__((packed)) {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_attributes;
} memory_map_entry_t;
```

读取项数：

```c
uint32_t count = *(volatile uint16_t*)0x4000;
```

复制到内核自己的数组：

```c
k_memcpy(e820_entries, (const void*)0x2000,
         count * sizeof(memory_map_entry_t));
```

复制后，即使以后实模式往返复用低内存，内核内存地图也不会丢失。

## 6. 双位图设计

NewOS 使用两张位图：

### 分配位图

```text
1 = 已分配或保留
0 = 可分配
```

用于首次适配和连续页分配。

### 保留位图

```text
1 = 永久保留，free_page/free_pages 无权释放
0 = 普通页
```

初始化流程：

1. 两张位图全部写成 `1`。
2. 对 E820 `usable` 区域做交集，打开对应页。
3. 重新保留内核镜像。
4. 保留两张位图自身。
5. 保留页目录和页表。

`free_page` 会检查保留位图：

```c
if (page >= total_pages || bit_get(reserved_bitmap, page)) return;
```

因此错误释放页表不会把内核脚下地板拆掉。

## 7. 当前分配范围

当前页表只把物理 `0..32MB` 恒等映射到内核虚拟地址。

所以 E820 可以显示更大内存，但页分配器暂时只管理：

```text
1MB .. 32MB
```

这是有意的阶段性限制。下一阶段会在 `paging` 中按需增加页表，让可管理
物理内存随 E820 扩展。

## 8. Shell 内存诊断

查看统计：

```text
meminfo
```

查看 E820：

```text
memmap
```

输出示例：

```text
[0] 0x00000000 - 0x0009FC00  usable
[1] 0x0009FC00 - 0x000A0000  reserved
...
```

查看内核堆：

```text
heap
heap blocks
```

运行压力测试：

```text
heaptest
```

## 9. 内核堆块结构

每个堆块有 24 字节头部：

```c
typedef struct heap_block {
    uint32_t magic;
    uint32_t size;
    uint32_t tail_magic;
    uint32_t flags;
    uint32_t pages;
    struct heap_block* next;
} heap_block_t;
```

字段作用：

- `magic`：堆块有效性。
- `size`：payload 容量，不包含头部。
- `tail_magic`：冗余校验，检测头部破坏。
- `flags`：空闲/使用中。
- `pages`：初始物理页数，用于合并统计。
- `next`：按物理地址排序的块链表。

`magic` 在内存中的十六进制值为：

```text
0x4B4D414C = "KMAL"
```

## 10. 分配、分裂和合并

### 首次适配

`kmalloc` 从块链表头部寻找第一个足够大的空闲块。

### 分裂

如果剩余空间足够容纳另一个块头：

```text
[ used N ][ free rest ]
```

而不是把整个大块浪费给一次小分配。

### 合并

`kfree` 后扫描相邻块。如果两个空闲块在物理内存上首尾相接：

```text
[ free A ][ free B ] → [ free A+B ]
```

这可以避免反复分配释放后产生大量碎片。

### 重分配

`krealloc`：

- `ptr == NULL`：等价于 `kmalloc`。
- 新大小不超过旧容量：直接返回原指针。
- 否则分配新块、复制旧内容、释放旧块。

## 11. 中断安全

堆操作会暂时关闭中断：

```c
static uint32_t irq_save_disable() {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
```

完成后只在原中断开启时恢复 `sti`。

物理页分配器同样使用保存中断状态的临界区。当前还没有 SMP 自旋锁；
单 CPU 课程内核用关中断保护即可，进入多核阶段后必须增加真正的
spinlock 和关中断锁组合。

## 12. 一个重要的编译器问题：#UD

实现 `heaptest` 时，GCC 16 使用：

```bash
-m32
```

但默认目标仍可能显示：

```text
-march=x86-64
-msse
-msse2
```

编译器把结构体清零优化成：

```asm
pxor %xmm0,%xmm0
movups %xmm0,...
```

NewOS 没有设置 `CR4.OSFXSR/OSXMMEXCPT`，执行 XMM 指令立即触发 #UD。

因此 Makefile 显式使用：

```make
-march=i386
-mno-mmx
-mno-sse
-mno-sse2
-mno-3dnow
-msoft-float
-fno-tree-vectorize
```

这说明“能编译出 32 位代码”不等于“产出的指令适合裸机 CPU 状态”。

## 13. 构建和运行

```bash
make clean
make
make run
```

启动串口应包含：

```text
[MEM] E820 entries=...
[MEM] selftest PASS
[HEAP] initialized
[HEAP] selftest PASS
```

## 14. 自动化验证

只运行本里程碑：

```bash
make test-memory
```

当前测试覆盖：

1. E820 解析标记。
2. 返回多个内存区域。
3. usable/reserved 类型存在。
4. 物理页分配自测。
5. 保留页不可释放。
6. 堆启动自测。
7. `meminfo`。
8. `memmap`。
9. `heap`。
10. `heap blocks`。
11. 多尺寸分配。
12. 数据完整性。
13. `krealloc` 内容保持。
14. 释放后完整性。
15. 压力测试后仍健康。

运行全部回归：

```bash
make test-all
```

该命令会依次运行：内存、开机画面、字体、文件系统、编辑器、C 编译器、
Ring 3、多进程、调度器、抢占调度和马里奥测试。

## 15. 建议自己完成的实验

### 实验 1：改变 QEMU 内存

```bash
qemu-system-i386 -m 64M -fda build/os.img -serial stdio
qemu-system-i386 -m 256M -fda build/os.img -serial stdio
```

观察 `memmap` 输出变化。当前 managed identity 区域仍限制为 32MB。

### 实验 2：故意释放保留页

在调试临时代码中尝试：

```c
free_page(0x01000000);
```

随后分配大量页，确认页表未被破坏。

### 实验 3：堆碎片

分配 100 个小块，释放偶数下标，再次分配不同大小，观察 `heap blocks`。

### 实验 4：堆破坏

临时写越界一个字节，检查 `heap_check()` 能否报错。生产代码不要保留这种
故意破坏。

### 实验 5：绘制 E820 图

让内核把所有 usable 区间映射成屏幕上的一行，理解内存空洞。

## 16. 常见错误

### Bootloader 显示 `E`

`E` 表示 BIOS 读盘失败。检查：

- 是否使用 `-fda build/os.img`。
- 启动驱动器号是否在 BIOS 调用前保存。
- E820 后是否恢复 DS/ES。
- 内核是否仍小于当前引导上限 180KB。

### `heaptest` 触发 #UD

反汇编故障 EIP：

```bash
addr2line -e build/kernel.elf -f -C <EIP>
objdump -d build/kernel.elf --start-address=<EIP-64> \
    --stop-address=<EIP+64>
```

若看到 `pxor/movups/xmm`，说明编译器生成了未启用的 SIMD 指令。

### 页分配返回 0

检查：

- E820 `usable` 区域是否被正确打开。
- 位图是否误把全部页标为已用。
- 连续页请求是否超过当前空闲连续范围。
- identity limit 是否仍为 32MB。

### `heap check=FAIL`

通常是越界写破坏块头。结合 `heap blocks`、GDB watchpoint 和 `heaptest`
缩小范围。

## 17. 本阶段验收标准

- [x] Bootloader 读取 E820 并保存项数。
- [x] Bootloader 在 BIOS 调用后恢复 DS/ES。
- [x] 内核复制并展示 E820。
- [x] 只分配 usable 区域。
- [x] 内核、位图和页表被永久保留。
- [x] `free_page` 不能释放保留页。
- [x] 实现 `kmalloc/kzalloc/kfree/krealloc`。
- [x] 实现块分裂与相邻空闲块合并。
- [x] 提供堆完整性和压力测试。
- [x] `memmap`、`heap`、`heap blocks`、`heaptest` 可交互使用。
- [x] 新增 15 项无头自动测试。
- [x] 原有文件系统、用户态、多进程和调度测试全部通过。

## 18. 下一章

里程碑 13 建议继续：

```text
完整虚拟地址空间
→ 按需分页
→ 写时复制 COW
→ guard page
→ brk/mmap
```

这一章会建立在当前 E820 和内核堆之上，逐步把“固定 4MB 用户区”升级为
真正的按进程虚拟内存管理。
