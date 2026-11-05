# 里程碑 12：用户堆 + waitpid + 精确孤儿回收

日期：2026-11-05

## 目标

里程碑 11 已经有独立地址空间、fork 和后台进程，但用户程序仍不能动态申请内存；父进程也没有 POSIX 风格的 `waitpid`。同时，旧清扫逻辑把槽位 5 以上的所有 ZOMBIE 都当成孤儿，一旦后台 shell 在活父进程等待期间启动新程序，就可能提前释放它的孩子。

本里程碑补齐三件事：

1. `sbrk` 系统调用与用户堆分配器；
2. `waitpid(pid/-1, &status)` 与退出状态传递；
3. 区分 detached 后台任务、真实子进程和孤儿僵尸。

## 用户地址空间布局

每个进程仍使用一整块连续的 4MB 用户物理区，虚拟地址固定为 `0x02000000..0x02400000`：

```
0x02000000  程序镜像 / BSS 预算          64KB
0x02010000  用户堆 (brk)                 ─┐
            堆向高地址增长                 ├ 中间区域约 3.875MB
0x023F0000  栈保护区起点                 ─┘
0x023F0000  用户栈 (向低地址增长)         64KB
0x02400000  栈顶 (第一个字节不可访问)
```

`sbrk(0)` 返回当前断点；正增量先清零新增范围，再推进 `task_t.brk`。负增量只收缩断点，物理页仍随进程存活。整段加减和上限检查都避免 32 位溢出，`INT_MIN` 也能安全处理。

用户库的 `malloc` 是 8 字节对齐的 bump allocator，`free` 是空操作。它足够支持短生命周期教学程序，但不是通用 allocator；真正的空闲链表/合并/回收留给后续内存管理里程碑。

## fork 语义

`fork` 现在继承父进程的 `brk`，并复制：

- 低 64KB 镜像/BSS 预算；
- `PROC_HEAP_BASE` 到父进程当前 `brk` 的已使用堆；
- 栈顶 64KB（子进程要在父进程的调用栈现场继续执行）。

测试程序在 fork 后让子进程把堆上的值从 1 改成 7。父进程随后读到 1，证明两边的堆物理页彼此独立。

## waitpid 与僵尸生命周期

`SYS_WAITPID` 的参数是 `pid` 和 `&status`：

- `pid > 0`：只等这个直接子进程；
- `pid == -1`：等任一个直接子进程；
- 返回被回收孩子的 pid，退出码写入用户指针；
- 没有匹配孩子返回 -1；
- 状态指针不属于用户地址范围时，内核在睡眠前拒绝。

收尸仍遵循里程碑 11 的安全顺序：孩子退出时只变 ZOMBIE；父进程跑在自己的 CR3 下时，`waitpid` 才释放孩子的 4MB 物理块和任务槽。

后台 `run prog &` 创建 `parent_pid=0` 的 detached 任务。孤儿清扫只在 `spawn` 时处理以下两类 ZOMBIE：

- detached 后台任务；
- 父任务已经消失或本身已是 ZOMBIE。

只要父进程仍存活，它的孩子就会一直保留到 `waitpid` 收尸。测试特意让孩子先退出成 ZOMBIE，再由 shell 启动另一个后台程序触发清扫，最后父进程仍能 `waitpid` 拿到 pid 与退出码 42。

## 踩坑

1. **新程序必须提供 `_start`**：用户镜像入口固定为 blob 首地址 `0x02000000`。最初 `memdemo` 只写了 `main`，链接器把静态 `putdec` 放在首位，CPU 直接在空栈顶执行 `ret`，现场是 `CR2=ESP=0x02400000`。修法：和现有程序一样把 `_start` 放进 `.text.start`。
2. **测试时序本身也会造成假阳性**：父进程最初只睡 90 tick，`ps` 和读屏完成前它已经退出，随后正常清扫被误判成“误收活父进程的孩子”。把延迟拉到 300 tick，确保 `ps`、第二个 `spawn` 和断言都落在父进程等待窗口内。

## 验证

- 新套件 `tools/test-memproc.py`：26 项，ALL PASS。
- 受影响的既有套件：`test-proc.py` 36 项、`test-user.py` 36 项、`test-sched.py` 15 项，ALL PASS。
- 项目测试总数从 284 更新为 310， README 里程碑徽章更新为 12/12。

## 文件

- 新：`user/memdemo.c`、`tools/test-memproc.py`
- 改：`kernel/proc.c/h`、`kernel/task.c/h`、`kernel/user.c/h`、`kernel/user_progs.c`、`kernel/user_embed.asm`、`user/lib.h`、`kernel/shell.c`、`Makefile`

## 下一步

- 空闲链表 allocator 与 `munmap`/堆回收；
- 写时复制 (COW) 或按需零页，替代 fork 的整段复制；
- 从文件系统加载 ELF/flat 程序并实现 `exec`；
- 用户态 shell 与管道/重定向系统调用。
