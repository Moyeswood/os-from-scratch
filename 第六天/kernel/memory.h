#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

void memory_init();
uint32_t alloc_page();
void free_page(uint32_t addr);

// 新增：获取内存统计信息
uint32_t get_total_pages();
uint32_t get_used_pages();

#endif
