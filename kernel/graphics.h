#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "types.h"

// 模式 13h 常量: 320x200, 256 色, 线性显存 0xA0000, 1 字节 = 1 像素
#define GFX_WIDTH  320
#define GFX_HEIGHT 200
#define GFX_FB ((uint8_t*)0xA0000)

// 默认调色板里的常用颜色 (gfx_init 设置)
enum {
    GFX_BLACK = 0,
    GFX_RED = 1,
    GFX_ORANGE = 2,
    GFX_YELLOW = 3,
    GFX_GREEN = 4,
    GFX_CYAN = 5,
    GFX_BLUE = 6,
    GFX_PURPLE = 7,
    GFX_WHITE = 15,
};

// 初始化: 设置默认 16 色调色板
void gfx_init();

// 设置调色板: 模式 13h 每通道 6 位 (0~63)
void gfx_set_color(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);

// 整屏填充
void gfx_clear(uint8_t color);

// 画点 (越界自动忽略)
void gfx_putpixel(int x, int y, uint8_t color);

// 实心矩形
void gfx_fillrect(int x, int y, int w, int h, uint8_t color);

// 贴位图/精灵 (data 按行存储, 每字节一个像素)
void gfx_blit(int x, int y, int w, int h, const uint8_t* data);

// 文字光标 (8x8 格子坐标)
void gfx_set_cursor(int x, int y);

// 图形模式画字符/字符串 (8x8 点阵字体)
void gfx_putc(char c);
void gfx_puts(const char* s);

// 库自测: 渐变 + 色块 + 文字
void gfx_demo();

#endif
