#include "serial.h"
#include "vga.h"
#include "idt.h"
#include "memory.h"
#include "heap.h"
#include "paging.h"
#include "shell.h"
#include "task.h"
#include "keyboard.h"
#include "realmode.h"
#include "timer.h"
#include "font.h"
#include "fs.h"
#include "splash.h"
#include "gdt.h"
#include "user.h"
#include "types.h"

extern void pic_remap();

void kernel_main() {
    serial_init();
    vga_init();
    memory_init();
    memory_selftest();
    paging_init();
    heap_init();
    heap_selftest();
    gdt_init();     // 里程碑 10: 换内核自己的 GDT (用户段 + TSS)
    idt_init();
    pic_remap();
    timer_init();   // PIT 通道 0: ~18.2 Hz 滴答 (游戏计时/蜂鸣超时用)

    // ★里程碑 11a: 调度器必须先于 sti 建好 — 第一个 IRQ0 就会走
    // task_timer_tick, 那时 tasks[] 必须已初始化。此时 shell 和
    // 演示任务都在睡, 开机画面在 idle 上下文 (本启动栈) 里跑。
    task_init();

    __asm__ volatile("sti");

    rm_load_trampoline();   // 把实模式跳板装入低内存 0x6000
    font_apply(18);         // 加载 Terminus 8x18 字体 (22 行) + 硬件自测 (串口报 [FONT])
    fs_init();              // 格式化并挂载内存文件系统 (串口报 [FS] + 自测)
    user_init();            // 里程碑 10: 用户区映射 + 程序表 (串口报 [USER])
    splash_run();           // 开机画面 (任意键跳过, 串口报 [SP] done/skipped)
    kbd_drain();            // 开机画面期间按的键清掉 (环形缓冲, 零开销)
    shell_reset_input();    // 跳过画面的按键别留到 shell 输入里

    vga_puts("MyQwen-OS v1.0.0\n\r");
    vga_puts("Type 'help' for available commands.\n\r\n\r");

    task_wake_shell();      // shell 就绪 — 下一个 IRQ0 把它切上 CPU
    sched_start();          // 空转循环, 永不返回 (没人可跑时 sti;hlt)
}
