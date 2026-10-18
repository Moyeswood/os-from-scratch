#include "vga.h"
#include "types.h"

// VGA 文本模式基址
static uint16_t* vga_buffer = (uint16_t*)0xB8000;
static int cursor_x = 0;
static int cursor_y = 0;

void vga_init() {
    for (int i = 0; i < 80 * 25; i++) {
        vga_buffer[i] = 0x0720; // 空格 + 白底黑字
    }
    cursor_x = 0;
    cursor_y = 0;
}

void vga_clear() {
    for (int i = 0; i < 80 * 25; i++) {
        vga_buffer[i] = 0x0720;
    }
    cursor_x = 0;
    cursor_y = 0;
}

void vga_putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        // 【终极修复】：在操作显存前关闭中断，操作完后恢复！
        // 确保 vga_putc 是一个原子操作，防止多任务切换导致的显存撕裂。
        __asm__ volatile("cli"); 
        
        int offset = cursor_y * 80 + cursor_x;
        vga_buffer[offset] = (0x0F << 8) | c; // 0x0F: 高亮白字
        
        cursor_x++;
        if (cursor_x >= 80) {
            cursor_x = 0;
            cursor_y++;
        }
        
        __asm__ volatile("sti"); 
    }

    if (cursor_y >= 25) {
        // 简单的滚屏逻辑（这里为了简化，直接清屏）
        vga_clear();
    }
}

void vga_puts(const char* s) {
    while (*s) {
        vga_putc(*s++);
    }
}
