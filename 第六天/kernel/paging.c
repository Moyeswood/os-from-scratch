#include "paging.h"
#include "serial.h"
#include "types.h"

#define PAGE_DIR_ADDR     0x1000000
#define PAGE_TABLE_0_ADDR 0x1001000
#define PAGE_TABLE_1_ADDR 0x1002000
#define PAGE_TABLE_2_ADDR 0x1003000
#define PAGE_TABLE_3_ADDR 0x1004000
#define PAGE_TABLE_4_ADDR 0x1005000
#define PAGE_TABLE_5_ADDR 0x1006000
#define PAGE_TABLE_6_ADDR 0x1007000
#define PAGE_TABLE_7_ADDR 0x1008000

void paging_init() {
    serial_puts("\n\r[Paging] Init (32MB map, tables at 16MB)...\n\r");

    uint32_t* page_dir = (uint32_t*)PAGE_DIR_ADDR;
    uint32_t* pt[8] = {
        (uint32_t*)PAGE_TABLE_0_ADDR, (uint32_t*)PAGE_TABLE_1_ADDR,
        (uint32_t*)PAGE_TABLE_2_ADDR, (uint32_t*)PAGE_TABLE_3_ADDR,
        (uint32_t*)PAGE_TABLE_4_ADDR, (uint32_t*)PAGE_TABLE_5_ADDR,
        (uint32_t*)PAGE_TABLE_6_ADDR, (uint32_t*)PAGE_TABLE_7_ADDR
    };

    for (int i = 0; i < 1024; i++) page_dir[i] = 0;
    for (int t = 0; t < 8; t++) {
        for (int i = 0; i < 1024; i++) pt[t][i] = 0;
    }

    for (int i = 0; i < 8; i++) {
        page_dir[i] = (uint32_t)pt[i] | PAGE_PRESENT | PAGE_WRITE;
    }

    for (int i = 0; i < 8192; i++) { 
        uint32_t physical_addr = i * 4096; 
        uint32_t* table = pt[i / 1024];
        int entry_idx = i % 1024;
        table[entry_idx] = physical_addr | PAGE_PRESENT | PAGE_WRITE;
    }

    serial_puts("[Paging] Tables set up (32MB).\n\r");

    __asm__ volatile("mov %0, %%cr3" : : "r"(PAGE_DIR_ADDR));
    serial_puts("[Paging] CR3 loaded.\n\r");

    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; 
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    
    serial_puts("[Paging] ENABLED!\n\r");
}
