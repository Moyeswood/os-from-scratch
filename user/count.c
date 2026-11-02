// ============================================================
//  用户程序: count — 演示用户态下 IRQ0 定时器照常滴答 (run count)
//
//  enter_user 把 iret 帧的 EFLAGS.IF 置 1, 所以用户态运行期间
//  硬件中断不停: 每 55ms 一次 IRQ0 进内核、加滴答、再回来,
//  用户程序完全无感 — 但它可以通过 SYS_GETTICKS 观察到。
// ============================================================
#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    uint32_t t0 = ticks();
    puts("\n\rcount: 1..15 (busy loop)\n\r");
    puts("count: ticks before = ");
    puthex(t0);
    puts("\n\r");
    for (int i = 1; i <= 15; i++) {
        putdec((uint32_t)i);
        putc(' ');
        for (volatile uint32_t d = 0; d < 3000000; d++);   // 忙等 (~0.5s TCG)
    }
    uint32_t t1 = ticks();
    puts("\n\rcount: ticks after = ");
    puthex(t1);
    puts("\n\r");
    if (t1 > t0) puts("count: timer kept ticking in user mode!\n\r");
    else         puts("count: BUG - ticks did not advance\n\r");
    exit(0);
}
