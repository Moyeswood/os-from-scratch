#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "types.h"

// DOS DEBUG 风格 monitor：真实暂停进程、单步、断点与继续。
void debug_run(const char* path);

// 用户态 #DB/#BP 异常入口；返回 1 表示调试器已接管该异常。
int debugger_exception(uint32_t* frame);

#endif
