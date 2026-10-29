#ifndef CC_H
#define CC_H

// c4 迷你 C 编译器移植 (C in four functions, Robert Swierczek)
// 支持的 C 子集: char/int/指针类型, if/while/return, 函数调用,
// 递归, 一维数组, enum, sizeof, 字符串, // 和 /* */ 注释,
// 内建: open/read/close/printf/malloc/free/memset/memcmp/exit
// 不支持: struct/union/float/for/do/switch/break/continue/宏
//
// path: 源文件路径 (作为 argv[0] 传给 main)
// argc/argv: 传给程序 main 的参数 (argv[0] = 源文件路径)
// 返回: 程序退出码; 负数 = 编译失败(-1) / 被 ESC 中断(-2)
int cc_run(const char* path, int argc, char** argv);

#endif
