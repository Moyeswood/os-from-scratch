#ifndef SHELL_H
#define SHELL_H

// 排空 8042 输出缓冲区 (游戏/编辑器退出前的释放码清理)
void kbd_drain(void);

// 初始化 Shell（打印提示符）
void shell_init();

// 处理键盘输入的字符
void shell_handle_char(char c);

// 清空输入缓冲 (开机画面等场景, 防止杂键进入命令)
void shell_reset_input(void);


#endif
