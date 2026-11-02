// ============================================================
//  用户程序: hello — 最小用户进程 (run hello)
//
//  运行在 Ring 3 (CPL=3)。注意它不 include 任何内核头文件,
//  也调不到 vga_puts: 内核代码页是 supervisor, 用户态一执行
//  就 #PF。每一行输出都走 int 0x80 系统调用。
// ============================================================
#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    puts("\n\rHello from Ring 3!\n\r");
    puts("  I am a USER process (CPL=3).\n\r");
    puts("  My code lives at 0x02000000, mapped with U/S=1.\n\r");
    puts("  Every character here went out through int 0x80.\n\r");
    exit(0);
}
