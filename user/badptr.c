// ============================================================
//  用户程序: badptr — 系统调用参数校验演示 (run badptr)
//
//  保护边界有两层: 页表 U/S 位拦住"用户直接读内核内存",
//  但用户还能通过系统调用"请内核帮忙读" — 把内核地址当参数
//  传给 SYS_PUTS。所以内核必须校验用户传来的指针:
//  落在用户区之外的一律拒绝 (真实 OS 的 copy_from_user
//  逐页复制比这里的首指针检查严格得多)。
// ============================================================
#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    puts("\n\rbadptr: passing a kernel pointer to SYS_PUTS...\n\r");
    int r = puts((const char*)0x10000);   // 内核镜像区 — 内核必须拒绝
    if (r < 0) puts("badptr: kernel rejected the pointer (good!)\n\r");
    else       puts("badptr: BUG - kernel accepted a kernel pointer\n\r");
    exit(0);
}
