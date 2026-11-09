#ifndef ASSEMBLER_H
#define ASSEMBLER_H

// 内置 32 位 Intel 语法汇编器。
// 成功返回 0，并生成可直接由 run 加载的 flat 用户程序。
int asm_run(const char* source_path, const char* output_path);

#endif
