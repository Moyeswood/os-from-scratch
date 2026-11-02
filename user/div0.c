// ============================================================
//  用户程序: div0 — 故意除零 (run div0)
//
//  除零 → #DE (向量 0)。演示第二类异常: 不是"越界"而是
//  "非法操作"。内核同样打印现场、杀掉进程、回 shell。
// ============================================================
#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    volatile int a = 42, b = 0;     // volatile: 防止编译器把除法常量折叠掉
    puts("\n\rdiv0: computing 42 / 0 ...\n\r");
    int c = a / b;                  // ← 这里触发 #DE (Divide Error)
    putc((char)c);                  // 永远走不到
    exit(0);
}
