#ifndef TASK_H
#define TASK_H
#include "types.h"

// ============================================================
//  里程碑 11a: PCB 调度器 (取代硬编码 4 任务轮转)
//
//  每个任务一个 PCB: pid / 名字 / 状态 / 上下文帧 / 内核栈。
//  任务 0 = idle (kernel_main 的启动栈, 只在没人可跑时执行);
//  任务 1 = shell 内核线程 (里程碑 11a 从 IRQ1 处理链里搬出来);
//  任务 2~4 = 抢占演示任务 A/B/C (start/stop 控制);
//  里程碑 11b: 用户进程也是任务 (独立页目录 + Ring 3 帧)。
//
//  状态机: READY → RUNNING → (yield/睡眠/被抢占) → READY/SLEEPING
//  SLEEPING 靠 wait_flags 说明等什么: 键盘事件 (IRQ1 唤醒) 或
//  时间 (IRQ0 唤醒)。无 wait_flags 的 SLEEPING = 睡死, 只能被
//  task_start_demo() 等显式唤醒。
// ============================================================

#define MAX_TASKS 16

typedef enum {
    TASK_IDLE,     // 调度器空转任务 (启动栈, 不参与轮转)
    TASK_READY,    // 就绪, 等 CPU
    TASK_RUNNING,  // 正在执行
    TASK_SLEEPING  // 睡眠 (wait_flags 说明等什么)
} task_state_t;

// 睡眠原因位掩码
#define TASK_WAIT_KBD  1   // 等键盘事件 (IRQ1 唤醒)
#define TASK_WAIT_TIME 2   // 等 g_ticks >= sleep_until (IRQ0 唤醒)

typedef struct {
    uint32_t esp;          // 上下文帧指针 (44 字节帧格式, 见 switch.asm)
    uint32_t pid;
    const char* name;
    uint8_t* kstack_top;   // 内核栈顶 (里程碑 11b: 切到该任务时 TSS.esp0 用它)
    task_state_t state;
    uint32_t wait_flags;
    uint32_t sleep_until;  // TASK_WAIT_TIME: 睡到 g_ticks >= 此值
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

task_t* task_current();       // 当前任务 (ps / 未来用户进程用)
void task_print_status();     // ps 命令

#endif
