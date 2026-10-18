#include "vga.h"
#include "io.h"

#define VIDEO_MEMORY 0xb8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

// 光标位置
static int cursor_x = 0;
static int cursor_y = 0;

// 颜色属性：黑底白字
static const uint8_t color = 0x0F;

void vga_init() {
    vga_clear();
}

void vga_clear() {
    char* vidptr = (char*)VIDEO_MEMORY;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT * 2; i += 2) {
        vidptr[i] = ' ';
        vidptr[i + 1] = 0x07;
    }
    cursor_x = 0;
    cursor_y = 0;
}

// 更新硬件光标位置
static void vga_update_cursor() {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    
    // 设置光标位置低字节
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    
    // 设置光标位置高字节
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

// 滚动屏幕
static void vga_scroll() {
    char* vidptr = (char*)VIDEO_MEMORY;
    
    // 把所有行上移一行
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            int src = (y * VGA_WIDTH + x) * 2;
            int dst = ((y - 1) * VGA_WIDTH + x) * 2;
            vidptr[dst] = vidptr[src];
            vidptr[dst + 1] = vidptr[src + 1];
        }
    }
    
    // 清空最后一行
    for (int x = 0; x < VGA_WIDTH; x++) {
        int pos = ((VGA_HEIGHT - 1) * VGA_WIDTH + x) * 2;
        vidptr[pos] = ' ';
        vidptr[pos + 1] = 0x07;
    }
    
    cursor_y = VGA_HEIGHT - 1;
}

void vga_putc(char c) {
    char* vidptr = (char*)VIDEO_MEMORY;
    
    if (c == '\n') {
        // 换行
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        // 回车
        cursor_x = 0;
    } else if (c == '\b') {
        // 退格
        if (cursor_x > 0) {
            cursor_x--;
            int pos = (cursor_y * VGA_WIDTH + cursor_x) * 2;
            vidptr[pos] = ' ';
            vidptr[pos + 1] = 0x07;
        }
    } else {
        // 普通字符
        int pos = (cursor_y * VGA_WIDTH + cursor_x) * 2;
        vidptr[pos] = c;
        vidptr[pos + 1] = color;
        cursor_x++;
        
        // 如果到达行尾，换行
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    
    // 如果到达屏幕底部，滚动
    if (cursor_y >= VGA_HEIGHT) {
        vga_scroll();
    }
    
    vga_update_cursor();
}

void vga_puts(const char* str) {
    while (*str) {
        vga_putc(*str);
        str++;
    }
}
