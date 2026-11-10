#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "user.h"

// ============================================================
//  里程碑 11b/13: 用户进程管理
//
//  用户进程 = 调度器里的一个任务, 自带:
//   - 独立页目录 (PDE0-7 复制内核 + 自己的用户 PDE)
//   - 按页分配的虚拟用户区 (32~36MB，不要求物理连续)
//   - 独立内核栈 (TSS.esp0: 它的中断/系统调用都落在这里)
//
//  内存模型 (里程碑 13):
//   - spawn 只装入实际 flat 镜像页
//   - 栈/heap/mmap 首次访问触发 #PF 补零页
//   - fork 共享页并标 COW，首次写时复制
//   - 栈底 guard page 与越界地址直接杀进程
//
//  生命周期:
//   proc_spawn(name,d) → 建任务 (READY), detached=1 表示无父进程 wait
//   proc_wait(pid)     → 睡到它 ZOMBIE, 收尸 (释放内存+空槽), 返回退出码
//   proc_exit(code)    → SYS_EXIT: 变 ZOMBIE 切走 (永不返回)
//   proc_fault_kill()  → 异常: 同上, 退出码 -1
//   proc_fork(frame)   → SYS_FORK: 复制地址空间+现场, 子进程返回 0
//   proc_kill(pid)     → kill 命令: 直接判死
//   proc_waitpid(...)  → SYS_WAITPID: 父进程等子进程并写退出状态
//
//  用户堆由 vm_brk 管理，mmap 由 vm_mmap 预留；fork 通过 COW 共享页。
//  后台进程 parent_pid=0，孤儿僵尸在 spawn 时清扫。
// ============================================================

int  proc_spawn(const char* name);     // 建用户进程任务, 返回 pid (失败 -1)
int  proc_spawn_file(const char* path); // 从内存 FS 的 flat 文件创建进程
int  proc_spawn_file_paused(const char* path); // 建 STOPPED 进程 (DEBUG)
int  proc_spawn_args(const char* name, int argc, char* const* argv,
                     int envc, char* const* envp, int detached);
int  proc_spawn_file_args(const char* path, int argc, char* const* argv,
                          int envc, char* const* envp, int detached);
int  proc_wait(int pid);               // 睡到它退出并收尸, 返回退出码 (-1=被杀)
int  proc_waitpid(int pid, int* status, int flags); // 用户父进程等待并回收直接子进程
int  proc_setpgid(int pid, int pgid);  // 设置进程组; pid/pgid=0 按 POSIX 语义
int  proc_set_foreground(int pgid);    // 设置控制台前台进程组 (0 = 无)
int  proc_signal(int pid, int sig);    // 正 pid 单进程, 负 pid 表示整组
int  proc_signal_foreground(int sig);  // 键盘 Ctrl+C 使用
int  proc_sigaction(int sig, uint32_t handler, uint32_t restorer);
int  proc_sigaction_flags(int sig, uint32_t flags);
int  proc_sigprocmask(int how, uint32_t set, uint32_t* oldset);
int  proc_sigreturn(user_frame_t* f);
void proc_deliver_signal(user_frame_t* f);
void proc_deliver_current_saved(void);
void proc_reset_signals(void);
int  proc_signal_pending_current(void);
int  proc_signal_restart_current(void);
void proc_exit(int code);              // SYS_EXIT 内核侧 (永不返回)
int  proc_fork(user_frame_t* f);       // SYS_FORK 内核侧: 父得子 pid, 子得 0
void proc_fault_kill(void);            // 异常处理调用 (永不返回)
int  proc_kill(int pid);               // kill 命令: 0 成功, -1 失败

#endif
