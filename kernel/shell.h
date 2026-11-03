#ifndef SHELL_H
#define SHELL_H

// 初始化 Shell（打印提示符）
void shell_init();

// 处理键盘输入的字符
void shell_handle_char(char c);

// 清空输入缓冲 (开机画面等场景, 防止杂键进入命令)
void shell_reset_input(void);

// 里程碑 11a: shell 主循环 — 作为内核线程被调度器拉起
// (task.c 的任务 1; 阻塞在 kbd_getchar_blocking 等输入)
void shell_main(void);

#endif
