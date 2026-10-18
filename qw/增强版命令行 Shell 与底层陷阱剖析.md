# 🧠 第四阶段：增强版命令行 Shell 与底层陷阱剖析（逐行注释版）

> **摘要**：本文档完整记录了操作系统开发第四阶段的核心内容。在基础 Shell 之上，我们成功实现了命令历史记录、参数解析（echo）、硬件直连（date/reboot）等高级功能。同时，本文档深度剖析了裸机开发中最隐蔽的陷阱——“局部数组导致的栈溢出崩溃”，并给出了终极解决方案。**所有代码均包含逐行详细注释**，是系统级编程的宝贵实战笔记。

---

## 🗺️ 一、 阶段目标与功能亮点

在实现了基础的命令解析后，本阶段致力于将 Shell 打造成一个真正具备“现代终端体验”的交互接口。

**核心功能亮点**：

1. **命令历史记录**：支持按“上/下方向键”翻阅之前输入的命令，极大提升交互效率。
2. **参数解析 (`echo`)**：能够提取命令后的文本参数并回显到屏幕。
3. **硬件直连 (`date` & `reboot`)**：直接读取主板 CMOS 芯片的实时时钟，以及通过键盘控制器端口实现系统热重启。
4. **内存透视 (`meminfo`)**：结合位图分配器，实时统计并展示物理内存的分配情况。

---

## 💻 二、 核心代码实现（逐行详细注释）

### 2.1 Shell 头文件：`kernel/shell.h`

#ifndef SHELL_H             // 头文件保护宏：防止该头文件被多次包含导致重复定义错误
#define SHELL_H             // 定义宏 SHELL_H，标记该头文件已被包含

// 初始化 Shell：在屏幕上打印 "OS> " 提示符，准备接收用户输入
void shell_init();

// 处理键盘输入的单个字符
// 参数 c: 从键盘中断转换而来的 ASCII 字符，或自定义的控制字符（如方向键）
void shell_handle_char(char c);

#endif                      // 结束头文件保护宏


### 2.2 键盘中断处理：`kernel/keyboard.c`

为了支持命令历史，我们需要在键盘驱动中捕获“上/下方向键”的扫描码，并将其转换为自定义的控制字符发送给 Shell。

#include "io.h"             // 引入底层 I/O 端口操作函数（如 inb, outb）
#include "serial.h"         // 引入串口驱动（本文件中未直接使用，但保持模块完整性）
#include "vga.h"            // 引入 VGA 显示驱动（本文件中未直接使用，由 Shell 处理显示）
#include "shell.h"          // 引入 Shell 接口，用于将字符发送给 Shell 处理
#include "types.h"          // 引入基础类型定义（如 uint8_t, int 等）

// 全局状态变量：记录 Shift 键是否被按下（0=未按下，1=按下）
static int shift_pressed = 0;
// 全局状态变量：记录 Caps Lock 键是否被开启（0=关闭，1=开启）
static int caps_lock_on = 0;

// 普通状态映射表（小写字母及基础符号）
// 数组索引对应键盘扫描码（0-127），数组值对应 ASCII 字符
// 例如：索引 0x1E (30) 对应字符 'a'
static char map_normal[128] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', // 0x00-0x0E
	'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', // 0x0F-0x1C
	0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',         // 0x1D-0x29
	0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,           // 0x2A-0x36
	'*', 0, ' ', 0,                                                          // 0x37-0x3A
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x3B-0x53
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x54-0x6B
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0              // 0x6C-0x7F
};

// Shift 状态映射表（大写字母及上档符号）
// 当 Shift 按下或 Caps Lock 开启时，使用此表进行查表
static char map_shift[128] = {
	0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
	'\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
	0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
	0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
	'*', 0, ' ', 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// 键盘中断处理函数（由中断描述符表 IDT 调用）
void keyboard_handler() {
	// 从键盘数据端口 0x60 读取 8 位扫描码
	uint8_t scancode = inb(0x60);
	
	// 判断是按键按下还是释放：扫描码最高位（第7位）为 1 表示释放，为 0 表示按下
	int is_release = (scancode & 0x80);
	// 屏蔽最高位，获取真实的 7 位扫描码（0-127）
	uint8_t key = scancode & 0x7F;

	// 处理左 Shift (0x2A) 和右 Shift (0x36)
	if (key == 0x2A || key == 0x36) {
		// 如果是按下，shift_pressed 设为 1；如果是释放，设为 0
		shift_pressed = !is_release;
		return; // Shift 键不产生字符，直接返回
	}

	// 处理 Caps Lock 键 (0x3A)
	// 只在按下时（!is_release）切换状态，防止释放时再次切换
	if (key == 0x3A && !is_release) {
		caps_lock_on = !caps_lock_on; // 翻转 Caps Lock 状态
		return; // Caps Lock 键不产生字符，直接返回
	}

	// 忽略所有按键释放事件，只在按下时处理字符，防止重复输入
	if (is_release) {
		return;
	}

	// 【新增】：处理上方向键 (扫描码 0x48)
	// 发送自定义控制字符 '\x11' 给 Shell，通知其翻阅上一条历史
	if (key == 0x48) {
		shell_handle_char('\x11');
		return;
	}
	// 【新增】：处理下方向键 (扫描码 0x50)
	// 发送自定义控制字符 '\x12' 给 Shell，通知其翻阅下一条历史
	if (key == 0x50) {
		shell_handle_char('\x12');
		return;
	}

	// 计算是否应该输出大写字母：使用异或逻辑
	// Shift 和 Caps Lock 只有一个开启时为大写，同时开启或同时关闭时为小写
	int is_upper = shift_pressed ^ caps_lock_on;
	
	// 根据大小写状态选择对应的映射表进行查表
	char c = is_upper ? map_shift[key] : map_normal[key];

	// 如果查表结果不为 0（即该扫描码对应有效字符）
	if (c != 0) {
		// 将字符发送给 Shell 处理，由 Shell 负责回显和缓冲
		shell_handle_char(c);
	}
}



### 2.3 增强版 Shell 核心逻辑：`kernel/shell.c`

这是本阶段最核心的文件，包含了历史记录管理、命令分发以及**修复栈溢出的关键代码**。
#include "shell.h"          // 引入 Shell 自身的接口声明
#include "vga.h"            // 引入 VGA 显示驱动（用于屏幕输出）
#include "serial.h"         // 引入串口驱动（用于调试日志输出）
#include "memory.h"         // 引入内存管理接口（用于 meminfo 命令）
#include "io.h"             // 引入底层 I/O 端口操作（用于 date/reboot 命令）
#include "types.h"          // 引入基础类型定义

#define MAX_INPUT 256       // 定义单行输入缓冲区的最大字符数
#define MAX_HISTORY 10      // 定义最多保存的历史命令条数

// 全局输入缓冲区，存储用户当前正在输入的字符串
static char input_buffer[MAX_INPUT];
// 当前缓冲区中已输入的字符数量（即光标位置）
static int input_index = 0;

// 命令历史记录缓冲区：二维数组，最多存 10 条，每条最长 256 字符
static char history[MAX_HISTORY][MAX_INPUT];
// 当前实际保存的历史命令总数
static int history_count = 0;
// 当前用户正在查看的历史命令索引（等于 history_count 时表示查看空白新行）
static int history_pos = 0;

// 自定义字符串比较函数（替代标准库 strcmp）
// 逐字符比较 s1 和 s2，直到遇到不同字符或字符串结束符 '\0'
static int shell_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; } // 指针同时后移
    return *(unsigned char*)s1 - *(unsigned char*)s2; // 返回差值，0 表示相等
}

// 自定义字符串长度计算函数（替代标准库 strlen）
static int shell_strlen(const char* s) {
    int len = 0;          // 长度计数器
    while (*s++) len++;   // 遍历字符串直到 '\0'，每次长度加 1
    return len;           // 返回总长度
}

// 自定义字符串复制函数（替代标准库 strcpy）
static void shell_strcpy(char* dst, const char* src) {
    while (*src) { *dst++ = *src++; } // 逐字符复制，直到 src 为 '\0'
    *dst = '\0';                      // 在目标字符串末尾添加结束符
}

// 【关键修复】：安全的十六进制打印函数
// 使用纯数学计算在寄存器中生成字符，彻底避免使用局部数组导致栈溢出崩溃
static void vga_put_hex(uint32_t num) {
    vga_puts("0x"); // 先打印十六进制前缀
    // 从最高 4 位（第 28 位）开始，每次右移 4 位，直到第 0 位
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (num >> i) & 0xF; // 提取当前的 4 位数据（0-15）
        char c;                            // 用于存储转换后的 ASCII 字符
        if (nibble < 10) {
            c = '0' + nibble;              // 0-9 转换为 ASCII '0'-'9'
        } else {
            c = 'A' + nibble - 10;         // 10-15 转换为 ASCII 'A'-'F'
        }
        vga_putc(c);                       // 将字符打印到 VGA 屏幕
    }
}

// 辅助函数：将 BCD 码（二进制编码的十进制）转换为普通二进制整数
// CMOS 时钟芯片返回的时间数据是 BCD 格式（例如 0x25 代表十进制的 25）
static uint8_t bcd_to_bin(uint8_t val) {
    return (val / 16) * 10 + (val % 16); // 高 4 位乘 10，加上低 4 位
}

// 辅助函数：读取主板 CMOS 实时时钟芯片的指定寄存器
static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg); // 向端口 0x70 写入要读取的寄存器地址
    return inb(0x71); // 从端口 0x71 读取该寄存器的数据并返回
}

// 重新打印当前输入行（用于切换历史记录时刷新屏幕显示）
static void redraw_input_line() {
    vga_puts("\rOS> "); // \r 是回车符，将光标移回当前行行首，然后打印提示符
    // 打印缓冲区中已有的字符
    for (int i = 0; i < input_index; i++) {
        vga_putc(input_buffer[i]);
    }
    // 用空格覆盖该行剩余的旧字符，防止长命令变短命令时出现残留
    for (int i = input_index; i < MAX_INPUT - 10; i++) {
        vga_putc(' ');
    }
    // 再次回车并打印提示符和当前字符，确保光标停在正确位置
    vga_puts("\rOS> "); 
    for (int i = 0; i < input_index; i++) {
        vga_putc(input_buffer[i]);
    }
}

// 1. help 命令：显示帮助信息
static void cmd_help() {
    vga_puts("\n\rAvailable commands:\n\r");
    vga_puts("  help     - Show this help message\n\r");
    vga_puts("  clear    - Clear the screen\n\r");
    vga_puts("  meminfo  - Show memory information\n\r");
    vga_puts("  echo [t] - Print text to screen\n\r");
    vga_puts("  date     - Show current time (CMOS)\n\r");
    vga_puts("  reboot   - Reboot the system\n\r");
    vga_puts("  version  - Show OS version\n\r");
}

// 2. echo 命令：回显文本（支持参数解析）
static void cmd_echo(char* cmd) {
    char* text = cmd + 5; // 指针向后移动 5 位，跳过 "echo " 这 5 个字符
    if (*text != '\0') {  // 如果后面还有内容
        vga_puts(text);   // 直接打印剩余文本
        vga_puts("\n\r"); // 打印换行
    }
}

// 3. date 命令：读取并显示 CMOS 实时时钟
static void cmd_date() {
    // 分别读取秒(0x00)、分(0x02)、时(0x04)寄存器，并转换为十进制
    uint8_t sec = bcd_to_bin(cmos_read(0x00));
    uint8_t min = bcd_to_bin(cmos_read(0x02));
    uint8_t hour = bcd_to_bin(cmos_read(0x04));
    
    vga_puts("\n\rCurrent Time: "); // 打印提示前缀
    // 逐位打印小时（十位和个位）
    vga_putc('0' + (hour / 10)); vga_putc('0' + (hour % 10)); vga_puts(":");
    // 逐位打印分钟
    vga_putc('0' + (min / 10)); vga_putc('0' + (min % 10)); vga_puts(":");
    // 逐位打印秒数
    vga_putc('0' + (sec / 10)); vga_putc('0' + (sec % 10));
    vga_puts("\n\r"); // 打印换行
}

// 4. reboot 命令：重启系统
static void cmd_reboot() {
    vga_puts("\n\rRebooting system...\n\r"); // 打印重启提示
    // 向键盘控制器命令端口 0x64 发送 0xFE（系统复位命令）
    outb(0x64, 0xFE);
    // 等待重启生效，执行HLT指令让CPU休眠
    while(1) { __asm__ volatile("hlt"); }
}

// 5. version 命令：显示系统版本
static void cmd_version() {
    vga_puts("\n\rMyQwen-OS v1.0.0 - Built with Qwen\n\r");
}

// 命令分发与执行器
static void execute_command() {
    input_buffer[input_index] = '\0'; // 在缓冲区末尾添加字符串结束符
    vga_puts("\n\r");                 // 打印换行，使命令输出在下一行显示

    // 根据输入缓冲区的字符串内容，分发到对应的命令处理函数
    if (input_index == 0) {
        // 如果用户只按了回车，输入为空，什么都不做
    } else if (shell_strcmp(input_buffer, "help") == 0) {
        cmd_help();
    } else if (shell_strcmp(input_buffer, "clear") == 0) {
        vga_clear(); // 清空 VGA 屏幕
    } else if (shell_strcmp(input_buffer, "meminfo") == 0) {
        // 获取内存统计信息并打印
        uint32_t total = get_total_pages();
        uint32_t used = get_used_pages();
        vga_puts("Memory: Total="); vga_put_hex(total);
        vga_puts(", Used="); vga_put_hex(used);
        vga_puts(", Free="); vga_put_hex(total - used); vga_puts("\n\r");
    } else if (shell_strcmp(input_buffer, "version") == 0) {
        cmd_version();
    } else if (shell_strcmp(input_buffer, "reboot") == 0) {
        cmd_reboot();
    } else if (shell_strcmp(input_buffer, "date") == 0) {
        cmd_date();
    } else if (input_buffer[0] == 'e' && input_buffer[1] == 'c' && input_buffer[2] == 'h' && input_buffer[3] == 'o' && input_buffer[4] == ' ') {
        cmd_echo(input_buffer); // 匹配 "echo " 开头的命令
    } else {
        // 未知命令处理
        vga_puts("Unknown command: ");
        vga_puts(input_buffer);
        vga_puts("\n\r");
    }

    // 将非空命令保存到历史记录中
    if (input_index > 0) {
        if (history_count < MAX_HISTORY) {
            // 如果历史记录未满，追加到末尾
            shell_strcpy(history[history_count], input_buffer);
            history_count++;
        } else {
            // 简化处理：如果满了，覆盖最老的一条（实际项目中应使用环形缓冲区）
            shell_strcpy(history[0], input_buffer); 
        }
    }
    
    history_pos = history_count; // 重置历史查看位置到最新状态
}

// 初始化 Shell：打印命令提示符
void shell_init() {
    vga_puts("OS> ");
}

// 核心字符处理函数：处理来自键盘驱动的各种字符
void shell_handle_char(char c) {
    if (c == '\n' || c == '\r') {
        // 处理回车键：执行当前输入的命令
        execute_command();
        input_index = 0;   // 清空缓冲区索引
        shell_init();      // 重新打印提示符
    } 
    else if (c == '\b') {
        // 处理退格键（Backspace）
        if (input_index > 0) {
            input_index--;             // 索引回退一位
            vga_putc('\b');            // 发送退格符，光标左移
            vga_putc(' ');             // 打印空格，覆盖原字符
            vga_putc('\b');            // 再次退格，光标回到正确位置
        }
    }
    else if (c == '\x11') { 
        // 处理上方向键：翻阅上一条历史命令
        if (history_pos > 0) {
            history_pos--;                                     // 历史索引减 1
            shell_strcpy(input_buffer, history[history_pos]);  // 复制历史命令到当前缓冲区
            input_index = shell_strlen(input_buffer);          // 更新缓冲区长度
            redraw_input_line();                               // 刷新屏幕显示
        }
    }
    else if (c == '\x12') { 
        // 处理下方向键：翻阅下一条历史命令
        if (history_pos < history_count) {
            history_pos++; // 历史索引加 1
            if (history_pos == history_count) {
                // 如果到了最新位置，清空当前输入行
                input_index = 0; 
                input_buffer[0] = '\0';
            } else {
                // 否则复制对应的历史命令
                shell_strcpy(input_buffer, history[history_pos]);
                input_index = shell_strlen(input_buffer);
            }
            redraw_input_line(); // 刷新屏幕显示
        }
    }
    else {
        // 处理普通可打印字符
        if (input_index < MAX_INPUT - 1) { // 防止缓冲区溢出
            input_buffer[input_index] = c; // 将字符存入缓冲区
            input_index++;                 // 索引前进一位
            vga_putc(c);                   // 实时回显到 VGA 屏幕
        }
    }
}


### 2.4 内核入口初始化：`kernel/kernel.c`


#include "serial.h"         // 引入串口驱动
#include "vga.h"            // 引入 VGA 显示驱动
#include "idt.h"            // 引入中断描述符表（IDT）初始化函数
#include "memory.h"         // 引入物理内存管理（位图分配器）
#include "paging.h"         // 引入虚拟内存分页机制
#include "shell.h"          // 引入命令行 Shell
#include "types.h"          // 引入基础类型定义

// 外部声明 PIC 重映射函数（定义在 pic.c 中）
extern void pic_remap();

// 内核主函数：操作系统的 C 语言入口点
void kernel_main() {
    // 1. 初始化串口，用于在 Linux 终端输出调试日志
    serial_init();
    serial_puts("\n\r[1] Serial OK\n\r");

    // 2. 初始化 VGA 文本模式，清屏并打印启动信息
    vga_init();
    vga_puts("MyQwen-OS Booting...\n");
    serial_puts("[2] VGA OK\n\r");

    // 3. 初始化物理内存管理器（解析 BIOS 内存地图，建立位图）
    memory_init();
    
    // 4. 初始化虚拟内存分页机制（建立恒等映射，开启 MMU）
    paging_init();
    
    // 5. 初始化中断描述符表（IDT）
    idt_init();
    
    // 6. 重新映射 8259A PIC，避免硬件中断与 CPU 异常冲突
    pic_remap();

    // 7. 开启 CPU 中断响应（执行 STI 指令）
    __asm__ volatile("sti");

    // 8. 初始化 Shell，打印命令提示符，准备接收用户输入
    vga_puts("System Ready. Type 'help' for commands.\n\r\n\r");
    shell_init();

    // 9. 进入内核空闲死循环
    // 执行 HLT 指令让 CPU 休眠，等待硬件中断（如键盘）唤醒
    while(1) {
        __asm__ volatile("hlt");
    }
}




---

## 🐛 三、 踩坑实录：局部数组导致的栈溢出崩溃

### 3.1 案发现场

在初步实现 `meminfo` 命令时，只要输入该命令，系统就会瞬间重启（Triple Fault），且没有任何错误日志输出。

### 3.2 原因剖析

罪魁祸首是 `vga_put_hex` 函数中的这一行代码：

const char hex_chars[] = "0123456789ABCDEF";

**底层原理**： 在裸机环境下，我们没有操作系统的虚拟内存和庞大的栈空间保护。函数内的局部数组会被分配到**栈（Stack）** 上。当键盘中断触发时，调用链为：`keyboard_handler` -> `shell_handle_char` -> `execute_command` -> `vga_put_hex`。 这个调用链本身已经消耗了一定的栈空间，而 `hex_chars` 数组的压栈直接导致了**栈溢出（Stack Overflow）**。溢出的数据覆盖了栈帧中的返回地址或关键寄存器，导致 CPU 执行了非法指令，瞬间崩溃重启。

### 3.3 终极解决方案

**裸机开发的铁律：永远不要在函数内部定义局部数组。** 我们将局部数组替换为**纯数学计算**，在寄存器中实时生成十六进制字符：

uint8_t nibble = (num >> i) & 0xF;
char c;
if (nibble < 10) {
    c = '0' + nibble;
} else {
    c = 'A' + nibble - 10;
}

这种写法不仅彻底消除了栈溢出风险，而且生成的机器码更高效，是系统级编程的标准实践。

---

## 🎯 四、 总结与展望

通过本阶段的开发，你的操作系统已经从一个“只能单向输出日志的黑盒”，蜕变为一个**能够理解人类指令、管理底层硬件、具备现代终端体验的交互式系统**。

你不仅掌握了命令解析、参数提取、硬件端口直连（CMOS/键盘控制器）等高级技巧，更通过实战深刻理解了裸机环境下栈空间的脆弱性，积累了宝贵的底层排错经验。

**接下来，我们将进入系统开发的深水区：掌握终极武器 —— GDB 源码级调试。** 告别“串口打印排错法”，向工业级 OS 开发标准迈进！