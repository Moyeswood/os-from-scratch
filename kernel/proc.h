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
//   proc_spawn(name)   → 建任务 (READY), 返回 pid
//   proc_wait(pid)     → 睡到它 ZOMBIE, 收尸 (释放内存+空槽), 返回退出码
//   proc_exit(code)    → SYS_EXIT: 变 ZOMBIE 切走 (永不返回)
//   proc_fault_kill()  → 异常: 同上, 退出码 -1
//   proc_fork(frame)   → SYS_FORK: 复制地址空间+现场, 子进程返回 0
//   proc_kill(pid)     → kill 命令: 直接判死
//
//  孤儿僵尸: 后台进程没人 wait → 下次 proc_spawn 时统一清扫
//  (sweep)。单 CPU + 清扫只在 spawn 时发生, 不会误扫正在等的孩子。
// ============================================================

int  proc_spawn(const char* name);     // 建用户进程任务, 返回 pid (失败 -1)
int  proc_wait(int pid);               // 睡到它退出并收尸, 返回退出码 (-1=被杀)
void proc_exit(int code);              // SYS_EXIT 内核侧 (永不返回)
int  proc_fork(user_frame_t* f);       // SYS_FORK 内核侧: 父得子 pid, 子得 0
void proc_fault_kill(void);            // 异常处理调用 (永不返回)
int  proc_kill(int pid);               // kill 命令: 0 成功, -1 失败

#endif
