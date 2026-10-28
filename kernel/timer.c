#include "timer.h"
#include "io.h"
#include "task.h"

volatile uint32_t g_ticks = 0;

static int beep_on = 0;
static uint32_t beep_start = 0;

void timer_init() {
    // 通道 0, 方波模式 (模式 3), 分频值写 0 = 8254 当作 65536
    // → 1193182 / 65536 ≈ 18.2 Hz, 和 BIOS INT 1Ah 滴答频率一致
    outb(0x43, 0x36);   // 通道 0: 先低后高字节, 方波模式
    outb(0x40, 0x00);
    outb(0x40, 0x00);
}

uint32_t timer_handler(uint32_t esp) {
    g_ticks++;

    // 蜂鸣响满 2 个滴答就关扬声器 (对应实模式版主循环里的 beep_service)
    if (beep_on && (g_ticks - beep_start) >= 2) {
        outb(0x61, inb(0x61) & 0xFC);   // 清 bit0 (定时器门控) + bit1 (扬声器使能)
        beep_on = 0;
    }

    // 抢占式调度: 返回下一个任务的帧指针 (未启用时原样返回)
    return task_timer_tick(esp);
}

void beep(uint16_t divisor) {
    outb(0x43, 0xB6);                   // 通道 2: 先低后高字节, 方波模式
    outb(0x42, divisor & 0xFF);
    outb(0x42, divisor >> 8);
    outb(0x61, inb(0x61) | 0x03);       // 打开扬声器 + 定时器门控
    beep_on = 1;
    beep_start = g_ticks;
}
