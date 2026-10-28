#ifndef TASK_H
#define TASK_H
#include "types.h"

typedef struct {
    uint32_t esp;   // 任务上下文帧指针 (帧格式见 switch.asm 注释)
} task_t;

// 多任务运行开关, 供 Shell 控制
extern volatile int multitasking_enabled;

void task_init();
void yield();
void task_print_status();

// IRQ0 抢占入口 (timer.c 的 timer_handler 调用):
// 传入当前任务的上下文帧指针, 返回下一个要恢复任务的帧指针
uint32_t task_timer_tick(uint32_t esp);

#endif
