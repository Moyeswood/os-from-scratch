#include "memory.h"
#include "serial.h"

#define PAGE_SIZE 4096
#define BITMAP_BASE 0x100000  // 位图放在 1MB 处

static uint8_t* bitmap;
static uint32_t bitmap_size;
static uint32_t total_pages;

void memory_init() {
    serial_puts("\n\r[Memory] Initializing memory manager...\n\r");
    
    // 从 0x500 读取 boot.asm 保存的扩展内存大小 (KB)
    uint16_t ext_mem_kb = *(uint16_t*)0x500;
    serial_puts("[Memory] Extended memory: ");
    serial_put_hex(ext_mem_kb);
    serial_puts(" KB\n\r");
    
    // 总内存 = 1MB (基础) + 扩展内存
    uint32_t total_mem = 1024 * 1024 + ext_mem_kb * 1024;
    total_pages = total_mem / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8;  // 向上取整到字节
    
    serial_puts("[Memory] Total pages: ");
    serial_put_hex(total_pages);
    serial_puts("\n\r");
    
    // 位图放在 1MB 处
    bitmap = (uint8_t*)BITMAP_BASE;
    
    // 初始化位图：全部标记为已分配 (1)
    for (uint32_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFF;
    }
    
    // 标记可用内存为空闲 (0)
    // 跳过 0-1MB (内核和位图占用)
    uint32_t usable_start = BITMAP_BASE + bitmap_size;
    // 对齐到页边界
    usable_start = (usable_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    uint32_t start_page = usable_start / PAGE_SIZE;
    for (uint32_t p = start_page; p < total_pages; p++) {
        uint32_t byte_idx = p / 8;
        uint8_t bit_idx = p % 8;
        if (byte_idx < bitmap_size) {
            bitmap[byte_idx] &= ~(1 << bit_idx);
        }
    }
    
    serial_puts("[Memory] Memory manager ready!\n\r");
}

uint32_t alloc_page() {
    for (uint32_t i = 0; i < bitmap_size; i++) {
        if (bitmap[i] != 0xFF) {
            for (int bit = 0; bit < 8; bit++) {
                if (!(bitmap[i] & (1 << bit))) {
                    bitmap[i] |= (1 << bit);
                    return (i * 8 + bit) * PAGE_SIZE;
                }
            }
        }
    }
    return 0;
}

void free_page(uint32_t addr) {
    uint32_t page = addr / PAGE_SIZE;
    uint32_t byte_idx = page / 8;
    uint8_t bit_idx = page % 8;
    if (byte_idx < bitmap_size) {
        bitmap[byte_idx] &= ~(1 << bit_idx);
    }
}

uint32_t get_total_pages() {
    return total_pages;
}

uint32_t get_used_pages() {
    uint32_t used = 0;
    for (uint32_t i = 0; i < bitmap_size; i++) {
        uint8_t byte = bitmap[i];
        // 计算 byte 中 1 的个数
        while (byte) {
            used += byte & 1;
            byte >>= 1;
        }
    }
    return used;
}
