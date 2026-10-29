#ifndef REALMODE_H
#define REALMODE_H

#include "types.h"

// 把实模式跳板二进制装入低内存 0x6000 (开机时调用一次)
void rm_load_trampoline(void);

// 切换到指定视频模式 (内部走实模式往返调用 INT 10h)
//   0x13 = 320x200x256 色图形模式
//   0x03 = 80x25 文本模式
// 注意: 调用期间 BIOS 会破坏 EBP/EBX/ESI/EDI, 外壳已负责保存恢复;
// 设置文本模式后 BIOS 会重载 ROM 字体 + 25 行, 需要重新调用
// font_apply() 恢复用户字体 (见 font.h)。
void rm_switch_mode(uint16_t mode);

#endif
