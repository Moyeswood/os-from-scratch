#include "vga.h"
#include "types.h"
#include "io.h"  // 【新增】：引入 io.h 以使用 outb

static uint16_t* vga_buffer = (uint16_t*)0xB8000;
static int cursor_x = 0;
static int cursor_y = 0;

// 【新增】：更新 VGA 硬件光标位置
static void vga_update_cursor() {
    uint16_t pos = cursor_y * 80 + cursor_x;
    outb(0x3D4, 0x0E); // 选择寄存器 14 (光标位置高 8 位)
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 0x0F); // 选择寄存器 15 (光标位置低 8 位)
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

void vga_init() {
    for (int i = 0; i < 80 * 25; i++) {
        vga_buffer[i] = 0x0720;
    }
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor(); // 初始化时也更新一次硬件光标
}

void vga_clear() {
    for (int i = 0; i < 80 * 25; i++) {
        vga_buffer[i] = 0x0720;
    }
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor(); // 清屏后重置光标
}

void vga_putc(char c) {
    // 【优化】：整个字符输出过程关闭中断，防止多任务切换导致显存/光标状态撕裂
    __asm__ volatile("cli");

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        int offset = cursor_y * 80 + cursor_x;
        vga_buffer[offset] = (0x0F << 8) | c;
        cursor_x++;
        if (cursor_x >= 80) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= 25) {
        vga_clear(); // 简单滚屏：直接清屏
    }

    vga_update_cursor(); // 【关键】：每次字符输出后，同步硬件光标！
    __asm__ volatile("sti");
}

void vga_puts(const char* s) {
    while (*s) {
        vga_putc(*s++);
    }
}
