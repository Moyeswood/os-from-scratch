#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#define MEMORY_MAP_ADDR       0x2000
#define MEMORY_MAP_COUNT_ADDR 0x4000
#define MEMORY_MAP_MAX        32
#define MEMORY_IDENTITY_LIMIT 0x02000000u  // 当前页表恒等映射 32MB

#define MEMORY_TYPE_USABLE        1
#define MEMORY_TYPE_RESERVED      2
#define MEMORY_TYPE_ACPI_RECLAIM  3
#define MEMORY_TYPE_ACPI_NVS      4
#define MEMORY_TYPE_BAD           5

typedef struct __attribute__((packed)) {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi_attributes;
} memory_map_entry_t;

void memory_init();
uint32_t alloc_page();
uint32_t alloc_pages(uint32_t n);  // 连续分配 n 页 (地址连续)
void free_page(uint32_t addr);
void free_pages(uint32_t addr, uint32_t n);  // 批量释放连续 n 页 (里程碑 11b)
void memory_reserve(uint32_t addr, uint32_t bytes);  // 保留一段物理地址

// 新增：获取内存统计信息
uint32_t get_total_pages();
uint32_t get_used_pages();
uint32_t get_free_pages();

// E820 内存地图. 内核只复制启动阶段给出的条目, 不直接信任任意长度.
uint32_t memory_map_count();
const memory_map_entry_t* memory_map_entry(uint32_t index);
uint32_t memory_map_usable_entries();
uint64_t memory_map_total_bytes();
uint64_t memory_map_usable_bytes();
uint32_t memory_identity_limit();
int memory_selftest();

#endif
