#ifndef TASK_H
#define TASK_H
#include "types.h"

typedef struct {
    uint32_t esp; 
} task_t;

void task_init();
void yield();
#endif
