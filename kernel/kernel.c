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

    vga_puts("System Ready. Cooperative Multitasking RESTORED.\n\r\n\r");
    
    task_init();
    yield();

    while(1) {
        __asm__ volatile("hlt");
    }
}
