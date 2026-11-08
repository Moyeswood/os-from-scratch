// ============================================================
//  开机画面 (模式 13h)
//
//  编排层: DAC 调色板编程 (深蓝渐变 16 色) → 渐变星空背景 →
//  NEWOS 大标题逐字揭示动画 → 任意键跳过或播完自动进 shell。
//
//  布局 (320x200):
//    "NEWOS"      y=68   主标题 (32px 大字, 逐字出现)
//    "MYQWEN-OS"  y=124  副标题 (16px, 白色)
//    60 颗星 + 深蓝渐变背景 (顶部亮 → 底部黑)
// ============================================================

#include "splash.h"
#include "realmode.h"
#include "font.h"
#include "timer.h"
#include "serial.h"
#include "io.h"
#include "keyboard.h"
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

// ---------- 按键/计时 ----------
// 里程碑 11a/11b: 任意键 = 环形缓冲里有"按下"事件。
// ★只认按下码 (<0x80): 触发 splash 命令的回车, 它的释放码还在
// 8042 传输途中 (drain 不再空转等待), 会晚到环形缓冲 — 释放码
// 不是"按键", 不能跳过动画 (老架构靠 kbd_drain 延时舞步解决,
// 新架构靠这里过滤)。
static int splash_anykey(void) {
    int sc;
    while ((sc = kbd_pop()) >= 0) {
        if (sc < 0x80) return 1;
    }
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

// ---------- 动画: NEWOS 逐字揭示 + 副标题 + 停留 ----------
static int splash_animate(void) {
    static const char* title = "NEWOS";
    static const char* sub   = "MYQWEN-OS";
    // 固定高对比色: 白、青、黄、绿、洋红。不随机，避免字母落进深色背景。
    static const uint8_t title_colors[5] = {
        0x0F, 0x0B, 0x0E, 0x0A, 0x0D
    };
    uint32_t t = g_ticks + 2;

    // 主标题 "NEWOS": 5 字 x (32+6 间距) - 6 = 184px → x = 68
    for (int i = 0; i < 5; i++) {
        splash_glyph(68 + i * 38, 68, title[i], title_colors[i], 4);
        if (splash_wait(t)) return 1;
        t += 3;
    }
    // 副标题 "MYQWEN-OS": 16px, 9 字 x 18 - 2 = 160px → x = 80
    for (int i = 0; sub[i]; i++) {
        splash_glyph(80 + i * 18, 124, sub[i], 0x0F, 2);
        if (splash_wait(t)) return 1;
        t++;
    }
    // 全部画完停留约 2.2 秒，期间任意键也跳过
    return splash_wait(g_ticks + 40);
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
