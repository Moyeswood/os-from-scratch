#ifndef HEAP_H
#define HEAP_H

#include "types.h"

void heap_init();
void* kmalloc(uint32_t size);
void* kzalloc(uint32_t size);
void* krealloc(void* ptr, uint32_t size);
void kfree(void* ptr);

int heap_check();
int heap_selftest();
void heap_print_stats();
void heap_print_blocks();

uint32_t heap_total_bytes();
uint32_t heap_used_bytes();
uint32_t heap_free_bytes();
uint32_t heap_block_count();
uint32_t heap_page_count();

#endif
