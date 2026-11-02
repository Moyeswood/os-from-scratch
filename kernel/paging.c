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
#define PAGE_TABLE_8_ADDR 0x1009000   // 里程碑 10: 用户区页表 (32~36MB)
// 用户区范围常量在 paging.h (USER_BASE/USER_SIZE)

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

// ============================================================
//  用户区映射 (里程碑 10: Ring 3 用户态)
//
//  页目录第 8 项 → 页表 8 → 物理 32~36MB, 每条目都带 PAGE_USER。
//  ★两级都要 U/S=1: CPU 检查页目录项和页表项, 有一级是 supervisor
//  用户态访问就 #PF。
//
//  共享地址空间设计: 内核 0~32MB (supervisor, 用户态碰了 #PF),
//  用户 32~36MB (USER)。不切 CR3 — 用户态发生中断/系统调用时
//  内核页都还在, 处理器无需切换页表。真实 OS 会给每个进程
//  独立页目录 (隔离更彻底), 这里作为下一步的引子。
// ============================================================
void paging_map_user_region() {
    uint32_t* page_dir = (uint32_t*)PAGE_DIR_ADDR;
    uint32_t* pt8 = (uint32_t*)PAGE_TABLE_8_ADDR;

    for (int i = 0; i < 1024; i++) {
        pt8[i] = (USER_BASE + (uint32_t)i * 4096)
               | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
    // PDE 也要 PAGE_USER (见函数头注释)
    page_dir[8] = (uint32_t)pt8 | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
}
