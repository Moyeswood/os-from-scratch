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

// 用户区常量 (32~36MB, 代码在 USER_BASE, 栈顶 USER_BASE+USER_SIZE)
#define USER_BASE 0x2000000
#define USER_SIZE 0x400000

// 里程碑 10: 把用户区 (32~36MB) 映射为 USER 页 (Ring 3 可访问)
void paging_map_user_region();

#endif
