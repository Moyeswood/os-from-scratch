#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

void memory_init();
uint32_t alloc_page();
void free_page(uint32_t addr);

#endif
