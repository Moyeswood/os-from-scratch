#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

// ============================================================
//  里程碑 11a: 键盘环形缓冲
//
//  IRQ1 处理器独占 8042: 读扫描码 → 推进环形缓冲 → 唤醒等待者。
//  shell/游戏/编辑器/cc/用户程序全部从这里读 (再没人直接轮询
//  8042 — 和处理器抢字节会互相偷走按键)。
//
//  缓冲里存的是"原始扫描码字节" (按下 <0x80, 释放 >=0x80,
//  0xE0 扩展键前缀独立占一个字节):
//   - shell/用户程序走 kbd_getchar*: 翻译成字符事件
//     (shift/caps 状态在 pop 时维护, 见 keyboard.c)
//   - 游戏/编辑器用 kbd_pop 读原始字节, 自己处理
//     (编辑器要 0xE0/Ctrl/自己的 shift 跟踪; mario 要释放码)
// ============================================================

void keyboard_handler();        // IRQ1 入口 (irq.asm 调用)

int  kbd_pop();                 // 取一个原始字节, 无则 -1
int  kbd_pop_blocking_press();  // 阻塞等下一个"按下"事件 (testmode 用)
int  kbd_getchar(int esc_to_27);        // 翻译成字符事件, 无则 -1
                                       // esc_to_27: ESC 返回 27 (用户程序) 还是忽略 (shell)
int  kbd_getchar_blocking(int esc_to_27);  // 阻塞版: 空则睡到键盘事件
int  kbd_check_esc();           // cc 失控保护: 缓冲里有 ESC 按下? (有则清空返回 1)
void kbd_drain();               // 清空环形缓冲 (老语义的排空释放码, 现在零开销)

#endif
