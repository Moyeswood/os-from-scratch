#ifndef FONT_H
#define FONT_H

#include "types.h"

// 应用指定字号的字体 (12/14/16), 内部走实模式往返调 BIOS INT 10h AX=1100h
// 加载后自动更新 VGA 行数 (33/28/25 行) 并清屏
void font_apply(uint8_t height);

// 当前字号
uint8_t font_current(void);

#endif
