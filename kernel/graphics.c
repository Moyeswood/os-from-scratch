// ============================================================
//  graphics.c — 模式 13h 图形库
//
//  模式 13h (320x200x256 色) 由实模式往返的 INT 10h 设置,
//  设置成功后线性显存在 0xA0000, 每个像素 1 字节, 值是
//  调色板索引 (256 色由 DAC 调色板定义, 每通道 6 位)。
//
//  本库只负责在显存里画东西, 不负责切模式 —
//  切模式用 realmode.h 的 rm_switch_mode(0x13)。
// ============================================================

#include "graphics.h"
#include "io.h"
#include "font8x8.h"

static int cursor_x = 0;   // 文字光标 (像素坐标, 8 对齐)
static int cursor_y = 0;

// ---------- 调色板 ----------

void gfx_set_color(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    // DAC 写流程: 先写 0x3C8 选择颜色索引, 再连续写 3 次 0x3C9 (R/G/B)
    outb(0x3C8, idx);
    outb(0x3C9, r);
    outb(0x3C9, g);
    outb(0x3C9, b);
}

void gfx_init() {
    // 默认 16 色调色板: 0=黑, 1~7 彩虹, 8=暗灰, 9~14 亮色变体, 15=白
    gfx_set_color(0, 0, 0, 0);
    gfx_set_color(1, 63, 0, 0);
    gfx_set_color(2, 63, 32, 0);
    gfx_set_color(3, 63, 63, 0);
    gfx_set_color(4, 0, 63, 0);
    gfx_set_color(5, 0, 63, 63);
    gfx_set_color(6, 0, 0, 63);
    gfx_set_color(7, 32, 0, 63);
    gfx_set_color(8, 32, 32, 32);
    gfx_set_color(9, 63, 32, 32);
    gfx_set_color(10, 63, 48, 32);
    gfx_set_color(11, 63, 63, 32);
    gfx_set_color(12, 32, 63, 32);
    gfx_set_color(13, 32, 63, 63);
    gfx_set_color(14, 32, 32, 63);
    gfx_set_color(15, 63, 63, 63);
    gfx_clear(GFX_BLACK);
}

// ---------- 基本绘图 ----------

void gfx_putpixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) {
        return;  // 越界自动忽略, 游戏代码可以放心乱画
    }
    GFX_FB[y * GFX_WIDTH + x] = color;
}

void gfx_clear(uint8_t color) {
    for (int i = 0; i < GFX_WIDTH * GFX_HEIGHT; i++) {
        GFX_FB[i] = color;
    }
}

void gfx_fillrect(int x, int y, int w, int h, uint8_t color) {
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            gfx_putpixel(xx, yy, color);
        }
    }
}

void gfx_blit(int x, int y, int w, int h, const uint8_t* data) {
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            uint8_t c = data[yy * w + xx];
            if (c != 0) {           // 0 = 透明色, 精灵背景不覆盖
                gfx_putpixel(x + xx, y + yy, c);
            }
        }
    }
}

// ---------- 文字 (8x8 点阵字体) ----------

void gfx_set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
}

void gfx_putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y += 8;
        return;
    }
    if (c == '\r') {
        cursor_x = 0;
        return;
    }
    if (c < 32 || c > 126) {
        return;  // 不可打印字符
    }

    const uint8_t* glyph = font8x8[c - 32];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (0x80 >> col)) {
                gfx_putpixel(cursor_x + col, cursor_y + row, GFX_WHITE);
            }
        }
    }

    cursor_x += 8;
    if (cursor_x >= GFX_WIDTH) {   // 到右边缘自动换行
        cursor_x = 0;
        cursor_y += 8;
    }
}

void gfx_puts(const char* s) {
    while (*s) {
        gfx_putc(*s++);
    }
}

// ---------- 库自测 ----------

void gfx_demo() {
    gfx_init();  // 确保调色板正确 (模式刚切换后 DAC 是默认的)

    // 1. 全屏色块渐变: 每 40x40 一块, 颜色 = (块行列和 % 8) + 1
    for (int y = 0; y < GFX_HEIGHT; y++) {
        for (int x = 0; x < GFX_WIDTH; x++) {
            gfx_putpixel(x, y, (uint8_t)((x / 40 + y / 40) % 8 + 1));
        }
    }

    // 2. 白色边框
    for (int x = 0; x < GFX_WIDTH; x++) {
        gfx_putpixel(x, 0, GFX_WHITE);
        gfx_putpixel(x, GFX_HEIGHT - 1, GFX_WHITE);
    }
    for (int y = 0; y < GFX_HEIGHT; y++) {
        gfx_putpixel(0, y, GFX_WHITE);
        gfx_putpixel(GFX_WIDTH - 1, y, GFX_WHITE);
    }

    // 3. 顶部信息栏 + 文字测试 (验证字体渲染)
    gfx_fillrect(8, 8, 304, 20, GFX_BLACK);
    gfx_set_cursor(16, 14);
    gfx_puts("NewOS Graphics Library v1.0");

    // 4. 字体会把 95 个可打印字符都画出来, 方便检查字形
    gfx_set_cursor(8, 40);
    gfx_puts("!\"#$%&'()*+,-./0123456789:;<=>?");
    gfx_set_cursor(8, 50);
    gfx_puts("@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_");
    gfx_set_cursor(8, 60);
    gfx_puts("`abcdefghijklmnopqrstuvwxyz{|}~");

    // 5. 几个实心矩形 + 精灵位图测试
    gfx_fillrect(8, 80, 40, 40, GFX_RED);
    gfx_fillrect(56, 80, 40, 40, GFX_GREEN);
    gfx_fillrect(104, 80, 40, 40, GFX_BLUE);

    // 一个 16x16 的"笑脸"位图, 用 gfx_blit 贴上 (0=透明)
    static const uint8_t smiley[16][16] = {
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,3,3,3,3,3,3,3,3,3,3,3,3,0,0},
        {0,3,3,3,3,3,3,3,3,3,3,3,3,3,3,0},
        {3,3,3,0,0,3,3,3,3,3,0,0,3,3,3,3},
        {3,3,0,0,0,0,3,3,3,0,0,0,0,3,3,3},
        {3,3,3,0,0,3,3,3,3,3,0,0,3,3,3,3},
        {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3},
        {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3},
        {3,3,0,0,0,3,3,3,3,3,0,0,0,3,3,3},
        {3,3,3,0,0,0,0,0,0,0,0,0,3,3,3,3},
        {3,3,3,3,0,0,0,0,0,0,0,3,3,3,3,3},
        {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3},
        {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3},
        {3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3},
        {0,3,3,3,3,3,3,3,3,3,3,3,3,3,3,0},
        {0,0,3,3,3,3,3,3,3,3,3,3,3,3,0,0},
    };
    gfx_blit(160, 80, 16, 16, (const uint8_t*)smiley);
}
