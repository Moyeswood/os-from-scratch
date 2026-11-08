#ifndef TASK_H
#define TASK_H
#include "types.h"
#include "fd.h"

// ============================================================
//  里程碑 11a/11b: PCB 调度器
//
//  每个任务一个 PCB: pid / 名字 / 状态 / 上下文帧 / 内核栈。
//  任务 0 = idle (kernel_main 的启动栈, 只在没人可跑时执行);
//  任务 1 = shell 内核线程; 任务 2~4 = 抢占演示 A/B/C;
//  任务 5+ = 用户进程 (里程碑 11b: 独立页目录 + Ring 3 帧)。
//
//  状态机: READY → RUNNING → (yield/睡眠/被抢占) → READY/SLEEPING
//  → (exit/被杀) → ZOMBIE。SLEEPING 靠 wait_flags 说明等什么:
//  键盘事件 (IRQ1 唤醒)、时间 (IRQ0 唤醒)、子进程退出 (WAIT_PROC)。
//  无 wait_flags 的 SLEEPING = 睡死, 只能被显式唤醒。
//
//  里程碑 11b 关键约定:
//   - 切换任务 = 换上下文帧 + (若是用户进程) 换 CR3 + TSS.esp0
//     —— 所有切换路径都走 switch_to() (见 task.c)
//   - 用户任务的上下文帧是 52 字节: 8 寄存器 + EIP + CS + EFLAGS
//     + ESP(用户) + SS(用户)。iret 看到 CS.RPL=3 会自动再弹
//     ESP/SS 落回用户栈 — 和内核任务的 44 字节帧共用同一套
//     popa+iret 恢复代码, switch.asm/irq.asm 无需改动。
//   - 里程碑 13: PCB 还记录 brk/mmap 边界；cr3 指向按页分配的
//     地址空间，不再假设用户物理页连续。
// ============================================================

#define MAX_TASKS 16
#define TASK_FD_MAX 16
#define TASK_SIGNAL_MAX 32

typedef enum {
    TASK_IDLE,     // 调度器空转任务 (启动栈, 不参与轮转)
    TASK_READY,    // 就绪, 等 CPU
    TASK_RUNNING,  // 正在执行
    TASK_SLEEPING, // 睡眠 (wait_flags 说明等什么)
    TASK_STOPPED,  // 里程碑 22: 作业暂停, SIGCONT 后可恢复
    TASK_ZOMBIE    // 里程碑 11b: 已退出, 等父进程收尸
} task_state_t;

// 睡眠原因位掩码
#define TASK_WAIT_KBD  1   // 等键盘事件 (IRQ1 唤醒)
#define TASK_WAIT_TIME 2   // 等 g_ticks >= sleep_until (IRQ0 唤醒)
#define TASK_WAIT_PROC 4   // 等子进程退出 (proc_exit 唤醒)
#define TASK_WAIT_PIPE 8   // 等管道数据/空间 (pipe 读写方唤醒)
#define TASK_WAIT_FS  16   // 等文件系统互斥锁

typedef struct {
    uint32_t esp;          // 上下文帧指针 (44/52 字节帧, 见 switch.asm)
    uint32_t pid;
    char name[16];         // 任务名 (用户进程: 程序名)
    uint8_t* kstack_top;   // 内核栈顶 (用户任务: TSS.esp0 用)
    task_state_t state;
    uint32_t wait_flags;
    uint32_t sleep_until;  // TASK_WAIT_TIME: 睡到 g_ticks >= 此值

    // ---- 里程碑 11b: 进程字段 ----
    uint32_t cr3;          // 页目录物理地址 (内核任务 = 内核页目录)
    int is_user;           // 用户进程标志
    int exit_code;         // 退出码 (ZOMBIE 时有效; 被杀 = -1)
    int parent_pid;        // 父任务 pid (wait 用)
    int pgid;              // 里程碑 21: 进程组 id
    // ---- 里程碑 13: 按需虚拟内存 ----
    uint32_t brk;          // 用户堆当前断点
    uint32_t mmap_brk;     // 匿名 mmap 窗口下一个空闲地址
    uint32_t image_end;    // 已装入镜像的页尾
    uint32_t cwd_inum;     // 进程工作目录 inode
    task_file_t* fds[TASK_FD_MAX];
    // ---- 里程碑 26: 用户态信号 ----
    uint32_t signal_pending;              // bit (signo-1)
    uint32_t signal_mask;                 // bit (signo-1)
    uint32_t signal_handlers[TASK_SIGNAL_MAX];
    uint32_t signal_restorer;             // 用户态 sigreturn 跳板地址
} task_t;

// 多任务演示开关 (start/stop 命令 + 演示任务 A/B/C 使用)
extern volatile int multitasking_enabled;

// 初始化: idle=启动栈, 建 shell/A/B/C (全部睡, 开机流程末尾才叫醒 shell)
void task_init();

// 空转循环 (kernel_main 末尾调, 永不返回): 有就绪任务时 IRQ0 会
// 在中断里直接切走; 这里只负责在没人可跑时 sti;hlt 等中断
void sched_start();

void task_wake_shell();       // 开机画面结束 → shell 就绪
void task_yield();            // 协作让出 CPU
void task_sleep_ticks(uint32_t n);   // 睡 n 滴答 (IRQ0 唤醒)
void task_sleep_forever();           // 睡死 (只能显式唤醒)
void task_wake_kbd();         // IRQ1 调用: 唤醒等键盘的任务
void task_start_demo();       // start 命令: A/B/C 就绪
void task_stop_demo();        // stop 命令: A/B/C 睡死

// 睡眠切换原语 (调用者已 cli): 把当前任务置为 SLEEPING 并切走。
// 被唤醒后返回 (仍在 cli 状态, 调用者负责 sti)。
// 竞态纪律: 调用者必须在 cli 下重查一次等待条件再睡 (见 keyboard.c)。
void task_sleep_switch_locked(uint32_t wait_flags, uint32_t until);

// IRQ0 抢占入口 (timer.c 调): 唤醒睡到点的任务 + 轮转抢占,
// 传入当前上下文帧指针, 返回下一个任务的帧指针
uint32_t task_timer_tick(uint32_t esp);

task_t* task_current();       // 当前任务
task_t* task_by_pid(int pid); // 按 pid 找任务 (无则 0)
task_t* task_at(int idx);     // 按槽位下标取任务 (proc.c 建任务用)

// ---- 里程碑 11b: 进程管理辅助 ----
int  task_alloc_slot();       // 找一个空闲任务槽 (返回下标, 无则 -1)
int  task_alloc_pid();        // 领一个不重复的 pid (单调递增)
void task_free_slot(int idx); // 重置任务槽 (收尸后)
void task_wake_pid(int pid);  // 唤醒等 WAIT_PROC 的睡眠者 (子进程退出时)
void task_wake_pipe(void);    // 管道状态变化时唤醒等待者
void task_wake_fs(void);      // 文件系统锁释放时唤醒等待者
void task_zombie_switch_away(int exit_code);  // 当前任务变 ZOMBIE 并切走, 永不返回
int  task_slot_of(task_t* t); // 任务在表里的下标
void task_fd_reset(task_t* t);
void task_fd_copy(task_t* dst, const task_t* src);
void task_fd_close_all(task_t* t);

void task_print_status();     // ps 命令

#endif
