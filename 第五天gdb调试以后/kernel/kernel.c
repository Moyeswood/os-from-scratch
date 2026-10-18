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
    vga_puts("MyQwen-OS Booting...\n");
    serial_puts("[2] VGA OK\n\r");

    memory_init();
    paging_init();
    idt_init();
    pic_remap();

    __asm__ volatile("sti");

    // 初始化 Shell
    vga_puts("System Ready. Type 'help' for commands.\n\r\n\r");
    shell_init();

    while(1) {
        __asm__ volatile("hlt");
    }
}
