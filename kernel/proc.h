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
int  proc_spawn_file(const char* path); // 从内存 FS 的 flat 文件创建进程
int  proc_wait(int pid);               // 睡到它退出并收尸, 返回退出码 (-1=被杀)
void proc_exit(int code);              // SYS_EXIT 内核侧 (永不返回)
int  proc_fork(user_frame_t* f);       // SYS_FORK 内核侧: 父得子 pid, 子得 0
void proc_fault_kill(void);            // 异常处理调用 (永不返回)
int  proc_kill(int pid);               // kill 命令: 0 成功, -1 失败

#endif
