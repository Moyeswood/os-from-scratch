#include "task.h"
#include "vga.h"
#include "memory.h"

static uint8_t stack_a[4096];
static uint8_t stack_b[4096];

static task_t kernel_task;
static task_t task_a_struct;
static task_t task_b_struct;
static task_t* current_task;

volatile int multitasking_enabled = 0; 

extern void switch_task(task_t* current, task_t* next);

void task_a_main() {
    while(1) {
        if (multitasking_enabled) {
            vga_putc('A');
            // 【优化】：缩短延时，减少打印频率
            for(volatile int i = 0; i < 1000000; i++); 
        } else {
            // 【关键修复】：如果多任务未启动，进入 hlt 休眠
            // 直到下一个中断（如键盘中断）唤醒
            __asm__ volatile("hlt");
        }
        yield(); 
    }
}

void task_b_main() {
    while(1) {
        if (multitasking_enabled) {
            vga_putc('B');
            for(volatile int i = 0; i < 1000000; i++);
        } else {
            __asm__ volatile("hlt");
        }
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
        current_task = &kernel_task; 
    }
    
    switch_task(prev, current_task);
}

void task_print_status() {
    vga_puts("\n\r  PID  NAME       STATUS\n\r");
    vga_puts("  -------------------------\n\r");
    
    vga_puts("   0   kernel     ");
    vga_puts(current_task == &kernel_task ? "RUNNING\n\r" : "READY\n\r");
    
    vga_puts("   1   task_a     ");
    vga_puts(current_task == &task_a_struct ? "RUNNING\n\r" : "READY\n\r");
    
    vga_puts("   2   task_b     ");
    vga_puts(current_task == &task_b_struct ? "RUNNING\n\r" : "READY\n\r");
    
    vga_puts("\n\r");
}
