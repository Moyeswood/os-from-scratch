#include "serial.h"
#include "vga.h"
#include "idt.h"
#include "memory.h"
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
    
    serial_puts("[5] Before alloc_page test\n\r");
    uint32_t page1 = alloc_page();
    serial_puts("[6] alloc_page 1: ");
    serial_put_hex(page1);
    serial_puts("\n\r");
    
    uint32_t page2 = alloc_page();
    serial_puts("[7] alloc_page 2: ");
    serial_put_hex(page2);
    serial_puts("\n\r");
    
    free_page(page2);
    uint32_t page3 = alloc_page();
    serial_puts("[8] alloc_page 3 (reuse): ");
    serial_put_hex(page3);
    serial_puts("\n\r");
    
    if (page3 == page2) {
        serial_puts("[9] Memory test SUCCESS\n\r");
    } else {
        serial_puts("[9] Memory test FAIL\n\r");
    }

    serial_puts("[10] Before idt_init\n\r");
    idt_init();
    serial_puts("[11] idt_init DONE\n\r");
    
    serial_puts("[12] Before pic_remap\n\r");
    pic_remap();
    serial_puts("[13] pic_remap DONE\n\r");

    serial_puts("[14] Before STI\n\r");
    __asm__ volatile("sti");
    serial_puts("[15] STI DONE (Interrupts Enabled)\n\r");

    serial_puts("\n\r[Kernel] System fully ready!\n\r");
    vga_puts("Memory & Interrupts OK!\n");
    vga_puts("Press keys...\n");

    while(1) {
        __asm__ volatile("hlt");
    }
}
