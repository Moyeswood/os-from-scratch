#ifndef TIMER_H
#define TIMER_H

#include "types.h"

// PIT 通道 0 驱动 IRQ0, 频率约 18.2 Hz (和 BIOS INT 1Ah 滴答频率一致,
// 游戏计时逻辑直接沿用实模式版的"滴答数"单位)
extern volatile uint32_t g_ticks;

void timer_init();     // 初始化 PIT 通道 0 (~18.2 Hz)
void timer_handler();  // IRQ0 处理: 滴答 +1, 蜂鸣超时自动关闭

// PC 扬声器发声. divisor = PIT 分频值, 频率 = 1193182 / divisor
// (实模式版用的频率: 5423=220Hz 落地, 1808=660Hz 暂存, 905=1318Hz 升级,
//  1356=880Hz 消行, 10847=110Hz 游戏结束)
// 响约 2 个滴答 (约 0.1 秒) 后由 timer_handler 自动关闭
void beep(uint16_t divisor);

#endif
