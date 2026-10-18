#include "task.h"
#include "vga.h"
#include "memory.h"

static uint8_t stack_a[4096];
static uint8_t stack_b[4096];

static task_t kernel_task;
static task_t task_a_struct;
static task_t task_b_struct;
static task_t* current_task;

extern void switch_task(task_t* current, task_t* next);

void task_a_main() {
    while(1) {
        vga_putc('A');
        for(volatile int i = 0; i < 5000000; i++); 
        yield(); 
    }
}

void task_b_main() {
    while(1) {
        vga_putc('B');
        for(volatile int i = 0; i < 5000000; i++);
        yield(); 
    }
}

static void create_task(task_t* task, void (*entry)(), uint32_t* stack_top) {
    uint32_t* stack = stack_top;
    
    *(--stack) = (uint32_t)entry; 
    *(--stack) = 0; // ebx
    *(--stack) = 0; // ecx
    *(--stack) = 0; // edx
    *(--stack) = 0; // esi
    *(--stack) = 0; // edi
    *(--stack) = 0; // ebp
    
    task->esp = (uint32_t)stack;
}

void task_init() {
    __asm__ volatile("mov %%esp, %0" : "=r"(kernel_task.esp));
    
    create_task(&task_a_struct, task_a_main, (uint32_t*)(stack_a + 4096));
    create_task(&task_b_struct, task_b_main, (uint32_t*)(stack_b + 4096));
    
    current_task = &kernel_task;
}

void yield() {
    task_t* prev = current_task;
    
    if (current_task == &kernel_task) {
        current_task = &task_a_struct;
    } else if (current_task == &task_a_struct) {
        current_task = &task_b_struct;
    } else {
        current_task = &task_a_struct;
    }
    
    switch_task(prev, current_task);
}
