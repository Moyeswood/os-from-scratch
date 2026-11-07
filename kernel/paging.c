#include "paging.h"
#include "memory.h"
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
#define PAGE_TABLE_30_ADDR 0x100A000  // 里程碑 11b: 草稿窗口页表 (虚拟 120~124MB)
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

    // 里程碑 11b: 草稿窗口 (虚拟 120~124MB → 页表 30), 初始为空
    {
        uint32_t* pt30 = (uint32_t*)PAGE_TABLE_30_ADDR;
        for (int i = 0; i < 1024; i++) pt30[i] = 0;
        page_dir[30] = (uint32_t)pt30 | PAGE_PRESENT | PAGE_WRITE;
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

// ============================================================
//  草稿窗口 (里程碑 11b: 每进程独立地址空间)
//
//  问题: 内核只恒等映射了物理 0~32MB。为新进程写页目录/页表/
//  用户区时, 目标物理页可能在 32MB 之外 — 内核根本没映射它。
//  方案: 固定虚拟窗口 0x7800000 (120MB, 4MB 大), 由页表 30 把
//  一段连续物理页临时映进来, 写完就摘掉。所有页目录 (内核的 +
//  每个进程的) 都挂同一个页表 30 — 内核代码在谁的 CR3 下都能
//  用窗口 (fork 在父进程 CR3 下拷贝时就用得上)。
//
//  ★窗口是全局共享资源: 使用者必须在关中断下成对调用, 否则
//  任务 A 映射到一半被抢占、任务 B 又映射别的目标, A 恢复后
//  写错地方 (proc.c 里 fork/spawn 都遵守)。
// ============================================================

uint32_t window_phys_base = 0;

void window_map(uint32_t phys_base, uint32_t n_pages) {
    uint32_t* pt30 = (uint32_t*)PAGE_TABLE_30_ADDR;
    for (uint32_t i = 0; i < n_pages && i < 1024; i++) {
        pt30[i] = (phys_base + i * 4096) | PAGE_PRESENT | PAGE_WRITE;
    }
    window_phys_base = phys_base;
    // 改页表后必须刷 TLB (没有 PGE, 重载 CR3 整刷最简单)
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

void window_unmap(void) {
    uint32_t* pt30 = (uint32_t*)PAGE_TABLE_30_ADDR;
    for (int i = 0; i < 1024; i++) pt30[i] = 0;
    window_phys_base = 0;
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

// ============================================================
//  里程碑 13: 动态用户地址空间基础
//
//  用户进程只预分配一个页目录。PDE8 对应的页表在第一次映射
//  32~36MB 内页面时才分配；每个虚拟页再单独绑定物理页。
// ============================================================

uint32_t paging_create_user_dir(void) {
    uint32_t pd_phys = alloc_page();
    if (!pd_phys) return 0;

    uint32_t* pd = (uint32_t*)pd_phys;
    uint32_t* kpd = (uint32_t*)PAGE_DIR_ADDR;
    for (int i = 0; i < 1024; i++) pd[i] = 0;

    // 内核恒等映射 0~32MB 与草稿窗口仍共享内核的页表。
    for (int i = 0; i < 8; i++) pd[i] = kpd[i];
    pd[30] = kpd[30];
    pd[31] = kpd[31];

    // PDE8 留给本进程的用户页表，先不分配。
    pd[8] = 0;
    return pd_phys;
}

static uint32_t* paging_user_pt(uint32_t pd_phys, uint32_t virt) {
    uint32_t pdi = virt >> 22;
    if (pdi < 8 || pdi >= 30) return 0;   // 只允许 32~120MB 的用户窗口
    uint32_t* pd = (uint32_t*)pd_phys;
    if (!(pd[pdi] & PAGE_PRESENT)) return 0;
    return (uint32_t*)(pd[pdi] & 0xFFFFF000u);
}

int paging_map_page(uint32_t pd_phys, uint32_t virt, uint32_t phys,
                    uint32_t flags) {
    uint32_t pdi = virt >> 22;
    if (pdi < 8 || pdi >= 30) return -1;
    if ((virt & 0xFFF) || (phys & 0xFFF)) return -1;

    uint32_t* pd = (uint32_t*)pd_phys;
    if (!(pd[pdi] & PAGE_PRESENT)) {
        uint32_t pt_phys = alloc_page();
        if (!pt_phys) return -1;
        uint32_t* pt = (uint32_t*)pt_phys;
        for (int i = 0; i < 1024; i++) pt[i] = 0;
        pd[pdi] = pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    uint32_t* pt = (uint32_t*)(pd[pdi] & 0xFFFFF000u);
    pt[(virt >> 12) & 0x3FF] = phys | PAGE_PRESENT | flags;
    return 0;
}

uint32_t* paging_user_pte(uint32_t pd_phys, uint32_t virt) {
    uint32_t* pt = paging_user_pt(pd_phys, virt);
    return pt ? &pt[(virt >> 12) & 0x3FF] : 0;
}

int paging_unmap_page(uint32_t pd_phys, uint32_t virt) {
    uint32_t* pte = paging_user_pte(pd_phys, virt);
    if (!pte || !(*pte & PAGE_PRESENT)) return -1;
    *pte = 0;
    return 0;
}

uint32_t paging_translate(uint32_t pd_phys, uint32_t virt) {
    uint32_t* pte = paging_user_pte(pd_phys, virt);
    if (!pte || !(*pte & PAGE_PRESENT)) return 0;
    return (*pte & 0xFFFFF000u) | (virt & 0xFFF);
}

void paging_invalidate_page(uint32_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void paging_flush_all(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}
