#include "shell.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"

#define MAX_INPUT 256

static char input_buffer[MAX_INPUT];
static int input_index = 0;

// 简单的字符串比较函数
static int shell_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// 简单的整数转十六进制字符串并打印到 VGA
static void vga_put_hex(uint32_t num) {
    const char hex_chars[] = "0123456789ABCDEF";
    vga_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        vga_putc(hex_chars[(num >> i) & 0xF]);
    }
}

// 执行命令
static void execute_command(char* cmd) {
    vga_puts("\n\r");
    serial_puts("\n\r");
    
    if (shell_strcmp(cmd, "help") == 0) {
        vga_puts("Available commands:\n\r");
        vga_puts("  help    - Show this help message\n\r");
        vga_puts("  clear   - Clear the screen\n\r");
        vga_puts("  meminfo - Show memory information\n\r");
        serial_puts("Available commands: help, clear, meminfo\n\r");
    } else if (shell_strcmp(cmd, "clear") == 0) {
        vga_clear();
        // 清屏后，shell_handle_char 会在 Enter 后重新打印提示符
    } else if (shell_strcmp(cmd, "meminfo") == 0) {
        uint32_t total = get_total_pages();
        uint32_t used = get_used_pages();
        uint32_t free_pages = total - used;
        
        vga_puts("Memory Information:\n\r");
        vga_puts("  Total pages: ");
        vga_put_hex(total);
        vga_puts("\n\r  Used pages:  ");
        vga_put_hex(used);
        vga_puts("\n\r  Free pages:  ");
        vga_put_hex(free_pages);
        vga_puts("\n\r");
        
        serial_puts("Memory Info: Total=");
        serial_put_hex(total);
        serial_puts(", Used=");
        serial_put_hex(used);
        serial_puts(", Free=");
        serial_put_hex(free_pages);
        serial_puts("\n\r");
    } else if (cmd[0] != '\0') {
        vga_puts("Unknown command: ");
        vga_puts(cmd);
        vga_puts("\n\r");
        serial_puts("Unknown command\n\r");
    }
    
    // 重新打印提示符
    shell_init();
}

void shell_init() {
    vga_puts("OS> ");
    serial_puts("OS> ");
}

void shell_handle_char(char c) {
    if (c == '\n' || c == '\r') {
        // 回车：执行命令
        input_buffer[input_index] = '\0';
        execute_command(input_buffer);
        input_index = 0;
    } else if (c == '\b') {
        // 退格：删除最后一个字符
        if (input_index > 0) {
            input_index--;
            vga_putc('\b');
            serial_putc('\b');
        }
    } else {
        // 普通字符：放入缓冲区并回显
        if (input_index < MAX_INPUT - 1) {
            input_buffer[input_index] = c;
            input_index++;
            vga_putc(c);
            serial_putc(c);
        }
    }
}
