#ifndef PAGING_H
#define PAGING_H

#include "types.h"

// 页表项和页目录项的标志位
#define PAGE_PRESENT  0x01  // 页在内存中
#define PAGE_WRITE    0x02  // 可写
#define PAGE_USER     0x04  // 用户态可访问 (Ring 3)

// 页表结构 (1024 个条目，每个 4 字节，共 4KB)
typedef struct {
    uint32_t entries[1024];
} page_table_t;

// 页目录结构 (1024 个条目，每个 4 字节，共 4KB)
typedef struct {
    uint32_t entries[1024];
} page_directory_t;

// 初始化分页机制
void paging_init();

#endif
