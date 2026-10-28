#include "shell.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "io.h"
#include "types.h"
#include "task.h"
#include "realmode.h"
#include "graphics.h"
#include "tetris.h"
#include "mario.h"

#define MAX_INPUT 256
#define MAX_HISTORY 10

static char input_buffer[MAX_INPUT];
static int input_index = 0;

// 命令历史记录
static char history[MAX_HISTORY][MAX_INPUT];
static int history_count = 0;
static int history_pos = 0; 

// 简单的字符串工具函数
static int shell_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static int shell_strlen(const char* s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static void shell_strcpy(char* dst, const char* src) {
    while (*src) { *dst++ = *src++; }
    *dst = '\0';
}

// 【关键修复】：用纯数学计算，彻底消灭局部数组，防止栈溢出崩溃！
static void vga_put_hex(uint32_t num) {
    vga_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (num >> i) & 0xF;
        char c;
        if (nibble < 10) {
            c = '0' + nibble;
        } else {
            c = 'A' + nibble - 10;
        }
        vga_putc(c);
    }
}

// 辅助：BCD 转二进制 (CMOS 时钟使用 BCD 编码)
static uint8_t bcd_to_bin(uint8_t val) {
    return (val / 16) * 10 + (val % 16);
}

// 辅助：读取 CMOS 寄存器
static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

// 【关键】排空 8042 输出缓冲区: 触发命令的那个键, 它的"释放码"一定会
// 晚到并残留在缓冲区里, 不排空的话会污染接下来的"等按键"逻辑。
// 所以: 清空 → 等释放码到达 → 还有字节就再清, 直到缓冲区安静。
static void kbd_drain() {
    do {
        while (inb(0x64) & 1) { inb(0x60); }           // 清空输出缓冲区
        for (volatile int i = 0; i < 20000000; i++);   // 给释放码留到达时间
    } while (inb(0x64) & 1);                           // 又来了字节 → 再清一轮
}

// 重新打印当前输入行（用于历史记录切换）
static void redraw_input_line() {
    vga_puts("\rOS> "); 
    for (int i = 0; i < input_index; i++) {
        vga_putc(input_buffer[i]);
    }
    // 用空格清除该行剩余的旧字符
    for (int i = input_index; i < MAX_INPUT - 10; i++) {
        vga_putc(' ');
    }
    vga_puts("\rOS> "); 
    for (int i = 0; i < input_index; i++) {
        vga_putc(input_buffer[i]);
    }
}

// 1. help 命令
static void cmd_help() {
    vga_puts("\n\rAvailable commands:\n\r");
    vga_puts("  help     - Show this help message\n\r");
    vga_puts("  clear    - Clear the screen\n\r");
    vga_puts("  meminfo  - Show memory information\n\r");
    vga_puts("  echo [t] - Print text to screen\n\r");
    vga_puts("  date     - Show current time (CMOS)\n\r");
    vga_puts("  reboot   - Reboot the system\n\r");
    vga_puts("  version  - Show OS version\n\r");
    vga_puts("  ps       - Show running tasks\n\r");
    vga_puts("  start    - Start multitasking demo\n\r");
    vga_puts("  stop     - Stop multitasking demo\n\r");
    vga_puts("  testmode - Test graphics mode round trip (BIOS INT 10h)\n\r");
    vga_puts("  tetris   - Play Tetris (ESC to exit)\n\r");
    vga_puts("  mario    - Play Super Mario (ESC to exit)\n\r");
}

// 2. echo 命令 (支持参数)
static void cmd_echo(char* cmd) {
    char* text = cmd + 5;
    if (*text != '\0') {
        vga_puts(text);
        vga_puts("\n\r");
    }
}

// 3. date 命令 (读取 CMOS)
static void cmd_date() {
    uint8_t sec = bcd_to_bin(cmos_read(0x00));
    uint8_t min = bcd_to_bin(cmos_read(0x02));
    uint8_t hour = bcd_to_bin(cmos_read(0x04));
    
    vga_puts("\n\rCurrent Time: ");
    vga_putc('0' + (hour / 10)); vga_putc('0' + (hour % 10)); vga_puts(":");
    vga_putc('0' + (min / 10)); vga_putc('0' + (min % 10)); vga_puts(":");
    vga_putc('0' + (sec / 10)); vga_putc('0' + (sec % 10));
    vga_puts("\n\r");
}

// 4. reboot 命令
static void cmd_reboot() {
    vga_puts("\n\rRebooting system...\n\r");
    outb(0x64, 0xFE);
    while(1) { __asm__ volatile("hlt"); }
}

// 5. version 命令
static void cmd_version() {
    vga_puts("\n\rMyQwen-OS v1.0.0 - Built with Qwen\n\r");
}

// 命令分发器
static void execute_command() {
    input_buffer[input_index] = '\0';
    vga_puts("\n\r");

    if (input_index == 0) {
        // 空命令
    } else if (shell_strcmp(input_buffer, "help") == 0) {
        cmd_help();
    } else if (shell_strcmp(input_buffer, "clear") == 0) {
        vga_clear();
    } else if (shell_strcmp(input_buffer, "meminfo") == 0) {
        uint32_t total = get_total_pages();
        uint32_t used = get_used_pages();
        vga_puts("Memory: Total="); vga_put_hex(total);
        vga_puts(", Used="); vga_put_hex(used);
        vga_puts(", Free="); vga_put_hex(total - used); vga_puts("\n\r");
    } else if (shell_strcmp(input_buffer, "version") == 0) {
        cmd_version();
    } else if (shell_strcmp(input_buffer, "reboot") == 0) {
        cmd_reboot();
    } else if (shell_strcmp(input_buffer, "start") == 0) {
        multitasking_enabled = 1;
        vga_puts("\n\rMultitasking STARTED. You will see A, B and C.\n\r");
        vga_puts("(C is a busy-loop task that never yields - only\n\r");
        vga_puts(" preemptive scheduling can interrupt it.)\n\r");
    } else if (shell_strcmp(input_buffer, "stop") == 0) {
        multitasking_enabled = 0;
        vga_puts("\n\rMultitasking STOPPED.\n\r");
    } else if (shell_strcmp(input_buffer, "ps") == 0) {
        task_print_status();
    } else if (shell_strcmp(input_buffer, "testmode") == 0) {
        vga_puts("\n\r[RM] Switching to mode 13h (320x200x256)...\n\r");

        // 实模式往返: 调 BIOS 切图形模式, 自动回到保护模式
        rm_switch_mode(0x13);
        serial_puts("[RM] Mode 13h set, back in protected mode!\n\r");

        gfx_demo();   // 图形库自测: 调色板/渐变/矩形/字体/精灵
        serial_puts("[RM] Press any key to return to text mode...\n\r");

        // 【关键】等待一个"新鲜"的按键。
        // 触发本命令的回车键, 它的"释放码"可能会晚到 (8042 输出缓冲区排队),
        // 直接等待会把它误当成"按任意键", 图形模式一闪而过。
        // 所以: 清空 → 等释放码到达 → 还有字节就再清, 直到缓冲区安静。
        do {
            while (inb(0x64) & 1) { inb(0x60); }   // 清空 8042 输出缓冲区
            for (volatile int i = 0; i < 20000000; i++);  // 给释放码留到达时间
        } while (inb(0x64) & 1);                   // 又来了字节 → 再清一轮

        // 现在等真正的下一个按键 (轮询 8042 状态端口 bit0 = 缓冲区有数据)
        while (!(inb(0x64) & 1)) { }
        inb(0x60);   // 读走扫描码, 清空输出缓冲区

        // 第二次往返: 切回文本模式
        rm_switch_mode(0x03);
        serial_puts("[RM] Text mode 03h restored, round trip OK!\n\r");

        vga_clear();
        vga_puts("MyQwen-OS v1.0.0\n\r");
        vga_puts("[RM] Round-trip test PASSED!\n\r");
    } else if (shell_strcmp(input_buffer, "tetris") == 0) {
        vga_puts("\n\r[GAME] Entering TETRIS... (ESC to exit)\n\r");

        // 切到模式 13h, 跑游戏, 再切回文本模式
        rm_switch_mode(0x13);
        serial_puts("[GAME] Mode 13h set, starting tetris...\n\r");
        gfx_init();     // 默认调色板 + 清屏 (游戏会再设自己的配色)
        tetris_run();
        serial_puts("[GAME] Tetris exited\n\r");

        // 【关键】排空 8042: 退出游戏的 ESC 键, 它的"释放码"还在缓冲区里,
        // 不排空的话会留到下一个"等按键"逻辑里被误读 (testmode 同款坑)
        kbd_drain();

        rm_switch_mode(0x03);
        serial_puts("[GAME] Text mode restored\n\r");

        vga_clear();
        vga_puts("MyQwen-OS v1.0.0\n\r");
    } else if (shell_strcmp(input_buffer, "mario") == 0) {
        vga_puts("\n\r[GAME] Entering SUPER MARIO... (ESC to exit)\n\r");

        // 切到模式 13h, 跑游戏, 再切回文本模式 (和 tetris 同一套流程)
        rm_switch_mode(0x13);
        serial_puts("[GAME] Mode 13h set, starting mario...\n\r");
        gfx_init();     // 默认调色板 + 清屏 (游戏会再设自己的配色)
        mario_run();
        serial_puts("[GAME] Mario exited\n\r");

        kbd_drain();    // 排空 ESC 释放码 (见 tetris 分支注释)

        rm_switch_mode(0x03);
        serial_puts("[GAME] Text mode restored\n\r");

        vga_clear();
        vga_puts("MyQwen-OS v1.0.0\n\r");
    } else if (shell_strcmp(input_buffer, "date") == 0) {
        cmd_date();
    } else if (input_buffer[0] == 'e' && input_buffer[1] == 'c' && input_buffer[2] == 'h' && input_buffer[3] == 'o' && input_buffer[4] == ' ') {
        cmd_echo(input_buffer);
    } else {
        vga_puts("Unknown command: ");
        vga_puts(input_buffer);
        vga_puts("\n\r");
    }

    // 保存非空命令到历史
    if (input_index > 0) {
        if (history_count < MAX_HISTORY) {
            shell_strcpy(history[history_count], input_buffer);
            history_count++;
        } else {
            shell_strcpy(history[0], input_buffer); 
        }
    }
    
    history_pos = history_count; 
}

void shell_init() {
    vga_puts("OS> ");
}

void shell_handle_char(char c) {
    if (c == '\n' || c == '\r') {
        execute_command();
        input_index = 0;
        shell_init();
    } 
    else if (c == '\b') {
        if (input_index > 0) {
            input_index--;
            vga_putc('\b');
            vga_putc(' ');
            vga_putc('\b');
        }
    }
    else if (c == '\x11') { // 上方向键
        if (history_pos > 0) {
            history_pos--;
            shell_strcpy(input_buffer, history[history_pos]);
            input_index = shell_strlen(input_buffer);
            redraw_input_line();
        }
    }
    else if (c == '\x12') { // 下方向键
        if (history_pos < history_count) {
            history_pos++;
            if (history_pos == history_count) {
                input_index = 0; 
                input_buffer[0] = '\0';
            } else {
                shell_strcpy(input_buffer, history[history_pos]);
                input_index = shell_strlen(input_buffer);
            }
            redraw_input_line();
        }
    }
    else {
        if (input_index < MAX_INPUT - 1) {
            input_buffer[input_index] = c;
            input_index++;
            vga_putc(c);
        }
    }
}
