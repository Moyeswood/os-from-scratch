#ifndef TASK_H
#define TASK_H
#include "types.h"

typedef struct {
    uint32_t esp; 
} task_t;

// 【新增】：多任务运行开关，供 Shell 控制
extern volatile int multitasking_enabled;

void task_init();
void yield();
void task_print_status();
#endif
