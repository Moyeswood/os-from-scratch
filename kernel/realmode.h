#ifndef REALMODE_H
#define REALMODE_H

#include "types.h"

// 把实模式跳板二进制装入低内存 0x6000 (开机时调用一次)
void rm_load_trampoline(void);

// 切换到指定视频模式 (内部走实模式往返调用 INT 10h)
//   0x13 = 320x200x256 色图形模式
//   0x03 = 80x25 文本模式
void rm_switch_mode(uint16_t mode);

#endif
