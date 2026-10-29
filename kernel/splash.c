// ============================================================
//  开机画面 (模式 13h)
//
//  编排层: DAC 调色板编程 (深蓝渐变 16 色) → 渐变星空背景 →
//  大标题逐字揭示动画 (字模绘制在 splash.asm, 用户的 hello.asm
//  32 位移植版) → 任意键跳过或播完自动进 shell。
//
//  布局 (320x200):
//    "HELLO"  y=28   主标题 (32px 大字, 颜色洗牌, 用户原版创意)
//    "WORLD"  y=76   主标题第二行 ("HELLO WORLD" 一行放不下, 拆两行居中)
//    "NEWOS!" y=132  副标题 (16px, 白色 — 浅灰与蓝底对比不足)
//    60 颗星 + 深蓝渐变背景 (顶部亮 → 底部黑)
// ============================================================

#include "splash.h"
#include "realmode.h"
#include "font.h"
#include "timer.h"
#include "serial.h"
#include "io.h"
#include "types.h"

extern void splash_glyph(int x, int y, char c, int color, int scale);

// ---------- DAC 调色板: 0x10..0x1F = 深蓝渐变 (顶部亮 → 底部黑) ----------
static void splash_palette(void) {
    outb(0x3C8, 0x10);                  // 调色板写索引从 0x10 开始
    for (int i = 0; i < 16; i++) {
        outb(0x3C9, 0);                 // R
        outb(0x3C9, 0);                 // G
        outb(0x3C9, (uint8_t)(i * 3));  // B: 0..45 亮度
    }
}

// ---------- 背景: 垂直渐变 + 星空 ----------
static void splash_bg(void) {
    // 渐变: 每扫描行一个颜色, 4 像素一组 32 位写入 (64000 像素要快)
    uint32_t* fb = (uint32_t*)0xA0000;
    for (int y = 0; y < 200; y++) {
        uint8_t c = (uint8_t)(0x10 + (199 - y) * 15 / 199);
        uint32_t v = (uint32_t)c | ((uint32_t)c << 8) | ((uint32_t)c << 16) | ((uint32_t)c << 24);
        uint32_t* row = fb + y * 80;    // 320 像素 = 80 个 32 位
        for (int i = 0; i < 80; i++) row[i] = v;
    }
    // 星星: 滴答种子 LCG (每次开机位置都不同)
    uint32_t seed = g_ticks | 1;
    for (int i = 0; i < 60; i++) {
        seed = seed * 1103515245 + 12345;
        int x = (int)((seed >> 8) % 320);
        int y = (int)((seed >> 16) % 200);
        uint8_t c = (uint8_t)(0x07 + (i % 3));   // 灰/深灰/白
        ((uint8_t*)0xA0000)[y * 320 + x] = c;
    }
}

// ---------- 颜色洗牌 (用户原版创意: 表首与随机位交换) ----------
static uint8_t splash_colors[16];

static void splash_shuffle(void) {
    static const uint8_t base[16] = {
        0x04, 0x0C, 0x0E, 0x0A, 0x02, 0x0B, 0x03, 0x09,
        0x0D, 0x05, 0x06, 0x01, 0x0F, 0x07, 0x08, 0x04
    };
    for (int i = 0; i < 16; i++) splash_colors[i] = base[i];
    uint32_t seed = g_ticks | 1;
    for (int n = 0; n < 16; n++) {
        seed = seed * 1103515245 + 12345;
        int j = (int)(seed % 16);
        uint8_t t = splash_colors[0];
        splash_colors[0] = splash_colors[j];
        splash_colors[j] = t;
    }
}

// ---------- 按键/计时 ----------
static int splash_anykey(void) {
    if (inb(0x64) & 1) { inb(0x60); return 1; }
    return 0;
}

// 等到 target 滴答, 期间任意键 → 返回 1 (跳过)
static int splash_wait(uint32_t target) {
    while (g_ticks < target) {
        if (splash_anykey()) return 1;
        __asm__ volatile("hlt");    // 睡到下一个中断 (IRQ0 滴答唤醒)
    }
    return 0;
}

// ---------- 动画: 逐字揭示 + 停留 ----------
static int splash_animate(void) {
    static const char* line1 = "HELLO";
    static const char* line2 = "WORLD";
    static const char* sub   = "NEWOS!";

    splash_shuffle();
    uint32_t t = g_ticks + 2;

    // 主标题 "HELLO WORLD": 32px 大字 11 个字符一行放不下 (320 宽),
    // 拆两行居中: 5 字 x (32+6 间距) - 6 = 184px → x = (320-184)/2 = 68
    for (int i = 0; i < 5; i++) {
        splash_glyph(68 + i * 38, 28, line1[i], splash_colors[i], 4);
        if (splash_wait(t)) return 1;
        t += 2;
    }
    for (int i = 0; i < 5; i++) {
        splash_glyph(68 + i * 38, 76, line2[i], splash_colors[5 + i], 4);
        if (splash_wait(t)) return 1;
        t += 2;
    }
    // 副标题 "NEWOS!": 16px, 6 字 x 17 - 1 = 101px → x = (320-101)/2 = 109
    for (int i = 0; sub[i]; i++) {
        splash_glyph(109 + i * 17, 132, sub[i], 0x0F, 2);   // 白色 (浅灰在蓝底上看不清)
        if (splash_wait(t)) return 1;
        t += 2;
    }
    // 全部画完停留 ~1.5 秒 (28 滴答), 期间任意键也跳过
    return splash_wait(g_ticks + 28);
}

// ---------- 主入口 ----------
void splash_run(void) {
    rm_switch_mode(0x13);
    serial_puts("[SP] splash\n\r");

    splash_palette();
    splash_bg();
    int skipped = splash_animate();

    rm_switch_mode(0x03);
    font_apply(font_current());   // 模式切换重载了 ROM 字体, 重新应用用户字号
    serial_puts(skipped ? "[SP] skipped\n\r" : "[SP] done\n\r");
}
