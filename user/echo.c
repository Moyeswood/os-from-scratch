// ============================================================
//  用户程序: echo — 键盘回显 (run echo, ESC 退出)
//
//  读键走 SYS_GETKEY: 内核替用户轮询 8042。为什么: 用户进程
//  跑在 shell 的 IRQ1 处理链里, 同优先级的 IRQ1 不会被投递
//  (和 tetris/mario 的游戏按键同款架构)。
// ============================================================
#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    puts("\n\recho: type anything, ESC to quit\n\r");
    for (;;) {
        int c = getkey();
        if (c == 27) break;
        if (c > 0) putc((char)c);
    }
    puts("\n\recho: bye!\n\r");
    exit(0);
}
