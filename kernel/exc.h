#ifndef EXC_H
#define EXC_H

#include "types.h"

// 异常处理总入口 (exc.asm 的 exc_common 调用)
// 参数 = 帧指针 (布局见 exc.asm 头部注释)
void exc_dispatch(uint32_t* frame);

#endif
