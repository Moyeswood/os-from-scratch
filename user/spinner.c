// ============================================================
//  用户程序: spinner — 无休止打点 (run spinner & 的演示)
//
//  后台运行后 shell 还能继续用, 点和你的输入同屏:
//    run spinner &   → 每 ~0.11 秒打一个点
//    ps              → 看到 spinner 在 RUNNING/SLEEPING 间切换
//    kill <pid>      → 结束它
//  打点节奏: sleep(2) = 2 滴答 (~110ms), 靠 IRQ0 唤醒 —
//  这就是"睡到定时器"的用户态用法。
// ============================================================
#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    puts("\n\rspinner: pid="); putdec((uint32_t)getpid());
    puts(" — dots every ~0.1s, kill me via 'kill <pid>'\n\r");
    uint32_t n = 0;
    for (;;) {
        putc('.');
        n++;
        if (n % 40 == 0) { putc('\n'); putc('\r'); }
        sleep(2);
    }
}
