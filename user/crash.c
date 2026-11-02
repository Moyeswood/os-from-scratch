// ============================================================
//  用户程序: crash — 故意写内核的 VGA 显存 (run crash)
//
//  0xB8000 在内核区 (supervisor 页, U/S=0)。用户态写它 →
//  CPU 检查页表发现 "用户在写 supervisor 页" → #PF (向量 14,
//  错误码 U=1 W=1 P=1)。内核打印现场后"杀掉"本进程回 shell。
//
//  这就是分页保护的实际效果: 用户程序再野也够不到内核。
//  (段保护也有一份: 平坦段下靠 U/S 位兜底。)
// ============================================================
#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    puts("\n\rcrash: writing to 0xB8000 (kernel VGA memory)...\n\r");
    *(volatile char*)0xB8000 = 'X';    // ← 这里触发 #PF
    puts("crash: this line should NEVER print\n\r");
    exit(0);
}
