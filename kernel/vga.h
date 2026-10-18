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

#endif
