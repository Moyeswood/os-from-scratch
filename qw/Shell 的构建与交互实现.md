# 🧠 第四阶段：命令行 Shell 的构建与交互实现

> **摘要**：本文档记录了操作系统开发中从“底层机制”走向“用户交互”的关键一步。在完成了物理内存管理与虚拟内存分页机制后，本阶段成功构建了一个支持实时回显、退格删除和命令解析的命令行 Shell，实现了 `help`、`clear`、`meminfo` 等基础命令，让内核真正具备了与人类交互的能力。

---

## 🗺️ 一、 阶段目标与设计思路

在开启分页机制后，我们的内核已经拥有了资源管理（内存分配）和硬件感知（中断与键盘）的能力。但在此之前，内核只能被动地打印日志，无法接收和理解用户的主动指令。

**本阶段的核心目标**：构建一个微型命令行 Shell（Command Line Interface）。

**设计思路与数据流向**：

1. **输入捕获**：用户按下键盘，触发硬件中断。
2. **中断处理**：`keyboard_handler` 拦截扫描码，将其转换为 ASCII 字符。
3. **字符分发**：不再直接打印字符，而是将字符发送给 Shell 的输入处理函数 `shell_handle_char`。
4. **缓冲与回显**：Shell 将字符存入全局输入缓冲区，并实时回显到 VGA 屏幕和串口。
5. **命令解析**：当检测到回车键（`\n` 或 `\r`）时，Shell 解析缓冲区中的字符串，匹配并执行对应的命令函数。

---

## 💻 二、 核心代码实现（含逐行详细注释）

### 2.1 Shell 头文件：`kernel/shell.h`

#ifndef SHELL_H
#define SHELL_H

// 初始化 Shell：在屏幕上打印 "OS> " 提示符
void shell_init();

// 处理键盘输入的单个字符
// 参数 c: 从键盘中断转换而来的 ASCII 字符
void shell_handle_char(char c);

#endif

### 2.2 Shell 核心逻辑：`kernel/shell.c`


#include "shell.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"

#define MAX_INPUT 256  // 定义输入缓冲区的最大长度

// 全局输入缓冲区，用于存储用户当前行输入的字符
static char input_buffer[MAX_INPUT];
// 当前缓冲区中已存储的字符数量（光标位置）
static int input_index = 0;

// 简单的字符串比较函数（替代标准库的 strcmp）
// 返回值：0 表示相等，非 0 表示不相等
static int shell_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// 简单的整数转十六进制字符串并打印到 VGA 屏幕
static void vga_put_hex(uint32_t num) {
    const char hex_chars[] = "0123456789ABCDEF";
    vga_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        vga_putc(hex_chars[(num >> i) & 0xF]);
    }
}

// 命令执行分发器
static void execute_command(char* cmd) {
    vga_puts("\n\r");
    serial_puts("\n\r");
    
    // 1. 处理 help 命令
    if (shell_strcmp(cmd, "help") == 0) {
        vga_puts("Available commands:\n\r");
        vga_puts("  help    - Show this help message\n\r");
        vga_puts("  clear   - Clear the screen\n\r");
        vga_puts("  meminfo - Show memory information\n\r");
        serial_puts("Available commands: help, clear, meminfo\n\r");
    } 
    // 2. 处理 clear 命令
    else if (shell_strcmp(cmd, "clear") == 0) {
        vga_clear(); // 清空 VGA 屏幕
        // 注意：清屏后不需要手动打印提示符，因为 execute_command 末尾会统一调用 shell_init()
    } 
    // 3. 处理 meminfo 命令
    else if (shell_strcmp(cmd, "meminfo") == 0) {
        // 获取内存统计信息
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
    } 
    // 4. 处理未知命令（忽略空输入）
    else if (cmd[0] != '\0') {
        vga_puts("Unknown command: ");
        vga_puts(cmd);
        vga_puts("\n\r");
        serial_puts("Unknown command\n\r");
    }
    
    // 执行完命令后，重新打印提示符，等待下一次输入
    shell_init();
}

// 初始化 Shell
void shell_init() {
    vga_puts("OS> ");
    serial_puts("OS> ");
}

// 核心：处理键盘输入的字符
void shell_handle_char(char c) {
    if (c == '\n' || c == '\r') {
        // 【回车键】：终止输入，执行命令
        input_buffer[input_index] = '\0'; // 添加字符串结束符
        execute_command(input_buffer);    // 分发并执行命令
        input_index = 0;                  // 重置缓冲区索引
    } 
    else if (c == '\b') {
        // 【退格键】：删除最后一个字符
        if (input_index > 0) {
            input_index--;                // 索引回退
            vga_putc('\b');               // 在 VGA 屏幕上回显退格（光标左移并擦除）
            serial_putc('\b');            // 在串口终端回显退格
        }
    } 
    else {
        // 【普通字符】：存入缓冲区并回显
        if (input_index < MAX_INPUT - 1) {
            input_buffer[input_index] = c; // 存入缓冲区
            input_index++;                 // 索引前进
            vga_putc(c);                   // VGA 回显
            serial_putc(c);                // 串口回显
        }
    }
}
### 2.3 配套修改：内存统计功能 (`kernel/memory.h` & `kernel/memory.c`)

为了支持 `meminfo` 命令，我们需要在内存管理器中暴露统计接口。

**`kernel/memory.h` 新增声明：**

// 获取系统总物理页数
uint32_t get_total_pages();

// 获取当前已分配（已使用）的物理页数
uint32_t get_used_pages();

**`kernel/memory.c` 新增实现：**

uint32_t get_total_pages() {
    return total_pages; // 直接返回初始化时计算的全局变量
}

uint32_t get_used_pages() {
    uint32_t used = 0;
    // 遍历位图的每一个字节
    for (uint32_t i = 0; i < bitmap_size; i++) {
        uint8_t byte = bitmap[i];
        // 计算当前字节中有多少个 1（即已分配的页）
        while (byte) {
            used += byte & 1; // 检查最低位是否为 1
            byte >>= 1;       // 右移一位，检查下一位
        }
    }
    return used;
}

### 2.4 配套修改：键盘中断接管 (`kernel/keyboard.c`)

这是最关键的一步：将键盘驱动与 Shell 连接起来。


// 在 keyboard_handler 函数的末尾，替换掉原来的直接打印逻辑：

if (c != 0) {
    // 【关键修改】：不再直接调用 serial_putc 和 vga_putc
    // 而是将字符交给 Shell 处理，由 Shell 决定是回显、退格还是执行命令
    shell_handle_char(c);
}
### 2.5 配套修改：内核入口初始化 (`kernel/kernel.c`)

在系统完全就绪后，启动 Shell。

    // ... 前面的内存、分页、中断初始化代码 ...

    serial_puts("[14] Before STI\n\r");
    __asm__ volatile("sti");
    serial_puts("[15] STI DONE (Interrupts Enabled)\n\r");

    // 【新增】：初始化 Shell，打印 OS> 提示符
    shell_init();
    serial_puts("\n\r[Kernel] Shell ready! Type 'help' for commands.\n\r");

    while(1) {
        __asm__ volatile("hlt");
    }

---

## 🎯 三、 运行效果与里程碑意义

### 3.1 预期终端输出

[15] STI DONE (Interrupts Enabled)
OS> 
[Kernel] Shell ready! Type 'help' for commands.

### 3.2 交互测试

- 输入 `help` 并回车，显示可用命令列表。
- 输入 `clear` 并回车，QEMU 屏幕被清空，随后重新出现 `OS>`。
- 输入 `meminfo` 并回车，显示十六进制的内存页统计信息（如 `Total pages: 0x00004000`）。
- 输入错误命令（如 `meninfo`），提示 `Unknown command`。
- 输入过程中按 `Backspace`，光标正确左移并删除字符。

### 3.3 里程碑意义

1. **完整的命令解析链路**：亲手打通了 `硬件中断 -> 扫描码转换 -> 输入缓冲 -> 字符串匹配 -> 命令执行` 的完整软件栈。
2. **状态管理**：Shell 能够优雅地处理退格、回车、未知命令，并在 `clear` 后恢复状态，具备了现代终端的雏形。
3. **从“黑盒”到“交互”**：操作系统不再是一个只能单向输出日志的机器，它开始能够理解并响应人类的主动意图。

---

## 🚀 四、 下一步展望

至此，我们的操作系统已经拥有了：

- ✅ **资源管理**（物理内存分配 + 虚拟内存分页）
- ✅ **硬件交互**（键盘驱动 + VGA 显示 + 串口日志）
- ✅ **用户接口**（命令行 Shell）

接下来，我们将进入系统开发的深水区：**掌握终极武器 —— GDB 源码级调试**。通过连接 QEMU 与 GDB，我们将实现 C 语言级别的单步调试、断点设置和变量查看，彻底告别“串口打印排错法”，向工业级 OS 开发标准迈进！
