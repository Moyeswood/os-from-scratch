#include "serial.h"
#include "vga.h"
#include "idt.h"
#include "memory.h"
#include "paging.h"
#include "shell.h"
#include "task.h"
#include "types.h"

extern void pic_remap();

void kernel_main() {
    serial_init();
    vga_init();
    memory_init();
    paging_init();
    idt_init();
    pic_remap();

    __asm__ volatile("sti");

    vga_puts("MyQwen-OS v1.0.0\n\r");
    vga_puts("Type 'help' for available commands.\n\r\n\r");
    
    shell_init();
    task_init();
    
    // 【关键】：Kernel 作为任务 0，主动参与轮转调度
    // 这样当轮到 Kernel 时，它会执行 yield() 让给 Task A，形成完美闭环
    while(1) {
        yield();
    }
}
