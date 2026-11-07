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
//   proc_sbrk(delta)   → SYS_SBRK: 用户堆扩容/收缩, 返回旧断点
//   proc_waitpid(...)  → SYS_WAITPID: 父进程等子进程并写退出状态
//
//  里程碑 12: 用户堆从 USER_BASE+64KB 开始, 到栈保护区之前结束。
//  fork 继承当前 brk 并复制已使用的堆页。后台进程 parent_pid=0,
//  没有父进程的僵尸在下一次 proc_spawn 时清扫; 仍有存活父进程的
//  ZOMBIE 保留下来, 由 waitpid 收尸。
// ============================================================

int  proc_spawn(const char* name);     // 建用户进程任务, 返回 pid (失败 -1)
int  proc_spawn_file(const char* path); // 从内存 FS 的 flat 文件创建进程
int  proc_spawn_args(const char* name, int argc, char* const* argv,
                     int envc, char* const* envp, int detached);
int  proc_spawn_file_args(const char* path, int argc, char* const* argv,
                          int envc, char* const* envp, int detached);
int  proc_wait(int pid);               // 睡到它退出并收尸, 返回退出码 (-1=被杀)
int  proc_waitpid(int pid, int* status); // 用户父进程等待并回收直接子进程
void proc_exit(int code);              // SYS_EXIT 内核侧 (永不返回)
int  proc_fork(user_frame_t* f);       // SYS_FORK 内核侧: 父得子 pid, 子得 0
void proc_fault_kill(void);            // 异常处理调用 (永不返回)
int  proc_kill(int pid);               // kill 命令: 0 成功, -1 失败

#endif
