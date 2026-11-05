#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "user.h"

// ============================================================
//  里程碑 11b: 用户进程管理
//
//  用户进程 = 调度器里的一个任务, 自带:
//   - 独立页目录 (PDE0-7 复制内核 + PDE30 草稿窗口 + 自己的 PDE8)
//   - 独立用户区 (虚拟 32~36MB → 自己的一整块物理内存)
//   - 独立内核栈 (TSS.esp0: 它的中断/系统调用都落在这里)
//
//  物理布局 (一次 alloc_pages(1026) 的连续块):
//   [0]      页目录 (1 页)
//   [1]      页表 8 — 用户区页表 (1 页)
//   [2..1025] 用户区 (4MB, 虚拟 USER_BASE..USER_BASE+USER_SIZE)
//
//  生命周期:
//   proc_spawn(name,d) → 建任务 (READY), detached=1 表示无父进程 wait
//   proc_wait(pid)     → 睡到它 ZOMBIE, 收尸 (释放内存+空槽), 返回退出码
//   proc_exit(code)    → SYS_EXIT: 变 ZOMBIE 切走 (永不返回)
//   proc_fault_kill()  → 异常: 同上, 退出码 -1
//   proc_fork(frame)   → SYS_FORK: 复制地址空间+现场, 子进程返回 0
//   proc_kill(pid)     → kill 命令: 直接判死
//   proc_sbrk(delta)   → SYS_SBRK: 用户堆扩容/收缩, 返回旧断点
//   proc_waitpid(...)  → SYS_WAITPID: 父进程等子进程并写退出状态
//
//  里程碑 12: 用户堆从 USER_BASE+64KB 开始, 到栈保护区之前结束。
//  fork 继承当前 brk 并复制已使用的堆页。后台进程 parent_pid=0,
//  没有父进程的僵尸在下一次 proc_spawn 时清扫; 仍有存活父进程的
//  ZOMBIE 保留下来, 由 waitpid 收尸。
// ============================================================

// 镜像/BSS 可用 64KB; 其后是用户堆, 栈顶向下保留 64KB。
#define PROC_IMAGE_BYTES 0x10000
#define PROC_HEAP_BASE   (USER_BASE + PROC_IMAGE_BYTES)
#define PROC_HEAP_LIMIT  (USER_BASE + USER_SIZE - 0x10000)

int  proc_spawn(const char* name, int detached); // 建进程, detached 无父 wait
int  proc_wait(int pid);               // 睡到它退出并收尸, 返回退出码 (-1=被杀)
int  proc_waitpid(int pid, int* status); // pid=-1 等任一个子进程, 返回其 pid
uint32_t proc_sbrk(int delta);         // 返回旧 brk, 越界/溢出返回 -1
void proc_exit(int code);              // SYS_EXIT 内核侧 (永不返回)
int  proc_fork(user_frame_t* f);       // SYS_FORK 内核侧: 父得子 pid, 子得 0
void proc_fault_kill(void);            // 异常处理调用 (永不返回)
int  proc_kill(int pid);               // kill 命令: 0 成功, -1 失败

#endif
