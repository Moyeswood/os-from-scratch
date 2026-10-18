#include "paging.h"
#include "serial.h"

// 【关键】：必须 4KB 对齐！这是 x86 硬件的强制要求。
// 我们定义 1 个页目录和 2 个页表（映射 8MB 内存）
__attribute__((aligned(4096))) page_directory_t kernel_page_directory;
__attribute__((aligned(4096))) page_table_t kernel_page_tables[2];

void paging_init() {
    serial_puts("\n\r[Paging] Initializing paging mechanism...\n\r");

    // 1. 清空页目录和页表
    for (int i = 0; i < 1024; i++) {
        kernel_page_directory.entries[i] = 0;
    }
    for (int i = 0; i < 2048; i++) {
        kernel_page_tables[0].entries[i] = 0;
        kernel_page_tables[1].entries[i] = 0;
    }

    // 2. 设置页目录项 (PDE)，指向页表
    kernel_page_directory.entries[0] = ((uint32_t)&kernel_page_tables[0]) | PAGE_PRESENT | PAGE_WRITE;
    kernel_page_directory.entries[1] = ((uint32_t)&kernel_page_tables[1]) | PAGE_PRESENT | PAGE_WRITE;

    // 3. 设置页表项 (PTE)，建立恒等映射 (虚拟地址 = 物理地址)
    // 映射前 8MB (2048 个页)
    for (int i = 0; i < 2048; i++) {
        uint32_t physical_addr = i * 4096; // 物理地址
        int table_idx = i / 1024;          // 哪个页表 (0 或 1)
        int entry_idx = i % 1024;          // 页表中的第几个条目
        
        kernel_page_tables[table_idx].entries[entry_idx] = physical_addr | PAGE_PRESENT | PAGE_WRITE;
    }

    serial_puts("[Paging] Page tables set up (Identity mapping 8MB).\n\r");

    // 4. 加载页目录到 CR3 寄存器
    __asm__ volatile("mov %0, %%cr3" : : "r"(&kernel_page_directory));
    serial_puts("[Paging] CR3 loaded.\n\r");

    // 5. 开启分页机制
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // 设置 PG 位
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    
    serial_puts("[Paging] Paging ENABLED! (CR0.PG = 1)\n\r");
}
