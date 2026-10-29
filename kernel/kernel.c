#include "serial.h"
#include "vga.h"
#include "idt.h"
#include "memory.h"
#include "paging.h"
#include "shell.h"
#include "task.h"
#include "realmode.h"
#include "timer.h"
#include "font.h"
#include "fs.h"
#include "splash.h"
#include "types.h"

extern void pic_remap();

void kernel_main() {
    serial_init();
    vga_init();
    memory_init();
    paging_init();
    idt_init();
    pic_remap();
    timer_init();   // PIT 通道 0: ~18.2 Hz 滴答 (游戏计时/蜂鸣超时用)

    __asm__ volatile("sti");

    rm_load_trampoline();   // 把实模式跳板装入低内存 0x6000
    font_apply(16);         // 加载 Terminus 8x16 漂亮字体 + 硬件自测 (串口报 [FONT])
    fs_init();              // 格式化并挂载内存文件系统 (串口报 [FS] + 自测)
    splash_run();           // 开机画面 (任意键跳过, 串口报 [SP] done/skipped)
    shell_reset_input();    // 跳过画面的按键别留到 shell 输入里

    vga_puts("MyQwen-OS v1.0.0\n\r");
    vga_puts("Type 'help' for available commands.\n\r\n\r");

    shell_init();
    task_init();
    
    // 【关键】：Kernel 作为任务 0，主动参与轮转调度
    // 这样当轮到 Kernel 时，它会执行 yield() 让给 Task A，形成完美闭环
    while(1) {
        yield();
    }
}
