#ifndef SPLASH_H
#define SPLASH_H

// 开机画面: 模式 13h 渐变星空 + 大标题逐字揭示动画
// (绘制核心是用户 hello.asm 的 32 位移植, 见 splash.asm)
// 任意键可跳过; 返回时已回到文本模式并重载用户字体。
// 串口标记: [SP] splash / done / skipped
void splash_run(void);

#endif
