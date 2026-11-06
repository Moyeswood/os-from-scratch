#ifndef PAGING_H
#define PAGING_H

#include "types.h"

// 页表项和页目录项的标志位
#define PAGE_PRESENT  0x01  // 页在内存中
#define PAGE_WRITE    0x02  // 可写
#define PAGE_USER     0x04  // 用户态可访问 (Ring 3)
#define PAGE_COW      0x200 // 软件位: fork 后共享、首次写时复制

// 页表结构 (1024 个条目，每个 4 字节，共 4KB)
typedef struct {
    uint32_t entries[1024];
} page_table_t;

// 页目录结构 (1024 个条目，每个 4 字节，共 4KB)
typedef struct {
    uint32_t entries[1024];
} page_directory_t;

// 内核页目录/页表物理地址 (paging.c; task.c 初始化 PCB.cr3 用)
#define PAGE_DIR_ADDR 0x1000000

// 初始化分页机制
void paging_init();

// 用户区常量 (32~36MB, 代码在 USER_BASE, 栈顶 USER_BASE+USER_SIZE)
#define USER_BASE 0x2000000
#define USER_SIZE 0x400000

// 里程碑 10: 把用户区 (32~36MB) 映射为 USER 页 (Ring 3 可访问)
void paging_map_user_region();

// 里程碑 11b: 草稿窗口 — 把连续物理页临时映到虚拟 0x7800000
// (120MB, 4MB 大), 供内核写新进程的页目录/页表/用户区。
// ★全局共享: 调用者必须在关中断下成对使用 (见 paging.c 注释)
#define WINDOW_BASE 0x7800000
void window_map(uint32_t phys_base, uint32_t n_pages);
void window_unmap(void);

// 里程碑 13: 按页操作页表。地址均为物理地址, 当前内核恒等映射
// 0~32MB, 因此分配器返回的页目录/页表可直接通过指针访问。
uint32_t paging_create_user_dir(void);
int paging_map_page(uint32_t pd_phys, uint32_t virt, uint32_t phys,
                    uint32_t flags);
int paging_unmap_page(uint32_t pd_phys, uint32_t virt);
uint32_t paging_translate(uint32_t pd_phys, uint32_t virt);
uint32_t* paging_user_pte(uint32_t pd_phys, uint32_t virt);
void paging_invalidate_page(uint32_t virt);
void paging_flush_all(void);

#endif
