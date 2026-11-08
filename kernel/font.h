#ifndef FONT_H
#define FONT_H

#include "types.h"

// 默认字号: 20px (20 行), 比原先的 18px 更易阅读。
#define FONT_HEIGHT_DEFAULT 20
#define FONT_HEIGHT_MIN 8
#define FONT_HEIGHT_MAX 32

// 应用当前字体的指定字号 (8..32), 自动生成缩放字模并更新 VGA 行数。
void font_apply(uint8_t height);

// 当前字号
uint8_t font_current(void);

// 切换字体风格并立即按当前字号重载; 成功返回 1。
int font_select(const char* name);

// 当前字体名称 (用于命令提示和串口诊断)
const char* font_current_name(void);

#endif
