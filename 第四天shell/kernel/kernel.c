#include "serial.h"
#include "vga.h"
#include "idt.h"
#include "memory.h"
#include "paging.h"
#include "shell.h"
#include "types.h"

extern void pic_remap();

void kernel_main() {
    serial_init();
    serial_puts("\n\r[1] Serial OK\n\r");

    vga_init();
    vga_puts("OS Booting...\n");
    serial_puts("[2] VGA OK\n\r");

    serial_puts("[3] Before memory_init\n\r");
    memory_init();
    serial_puts("[4] memory_init DONE\n\r");

    serial_puts("[4.5] Before paging_init\n\r");
    paging_init();
    serial_puts("[4.6] paging_init DONE\n\r");
    
    serial_puts("[10] Before idt_init\n\r");
    idt_init();
    serial_puts("[11] idt_init DONE\n\r");
    
    serial_puts("[12] Before pic_remap\n\r");
    pic_remap();
    serial_puts("[13] pic_remap DONE\n\r");

    serial_puts("[14] Before STI\n\r");
    __asm__ volatile("sti");
    serial_puts("[15] STI DONE (Interrupts Enabled)\n\r");

    // 初始化 Shell
    shell_init();
    serial_puts("\n\r[Kernel] Shell ready! Type 'help' for commands.\n\r");

    while(1) {
        __asm__ volatile("hlt");
    }
}
