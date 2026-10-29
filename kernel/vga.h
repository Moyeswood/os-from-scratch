#ifndef VGA_H
#define VGA_H

#include "types.h"

// 初始化 VGA
void vga_init();

// 在屏幕上打印一个字符
void vga_putc(char c);

// 在屏幕上打印字符串
void vga_puts(const char* str);

// 清屏
void vga_clear();

// 设置/查询屏幕行数 (字号变化时由 font.c 更新: 25/28/33 行)
void vga_set_rows(int rows);
int  vga_get_rows();

// 编辑器用: 直接写显存 (不碰光标, 用于整屏重绘), attr = 属性字节
// (0x0F 白字黑底, 0x70 反显); 以及只移动硬件光标
void vga_putc_attr_xy(int x, int y, char c, uint8_t attr);
void vga_move_cursor(int x, int y);

#endif
