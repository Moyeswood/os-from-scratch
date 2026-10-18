---
tags:
  - OSDev
  - Interrupts
  - Keyboard
  - VGA
  - Complete_Guide
date: 2026-10-05
status: 🟢 完成
---

# ⚡ 第二阶段终极指南：中断系统与键盘驱动（完整版）

> [!abstract] 本文档包含
> - 完整的实现过程
> - 每行代码的详细注释
> - 所有错误的排查与解决
> - 核心知识点总结

---

## 💻 完整源代码（每行都有注释）

### 📂 1. 类型定义：`kernel/types.h`

```c
#ifndef TYPES_H              // 防止头文件被重复包含
#define TYPES_H

typedef unsigned char uint8_t;      // 定义 8 位无符号整数（0-255）
typedef unsigned short uint16_t;    // 定义 16 位无符号整数（0-65535）
typedef unsigned int uint32_t;      // 定义 32 位无符号整数（0-4294967295）

#endif
```

---

### 📂 2. IDT 结构定义：`kernel/idt.h`

```c
#ifndef IDT_H                // 防止头文件被重复包含
#define IDT_H

#include "types.h"           // 引入类型定义

// IDT 条目结构（8 字节）
// 每个条目存储一个中断处理程序的地址和属性
struct idt_entry {
	uint16_t base_lo;       // 处理程序地址的低 16 位
	uint16_t sel;           // 内核段选择子（我们用 0x08，指向代码段）
	uint8_t always0;        // 必须为 0（Intel 规定）
	uint8_t flags;          // 类型和属性（0x8E = 32 位中断门，Ring 0）
	uint16_t base_hi;       // 处理程序地址的高 16 位
} __attribute__((packed));    // 禁止编译器对齐优化，确保结构体紧凑

// IDT 指针结构（6 字节）
// 告诉 CPU IDT 的位置和大小
struct idt_ptr {
	uint16_t limit;         // IDT 的大小（字节数 - 1）
	uint32_t base;          // IDT 的起始地址
} __attribute__((packed));    // 禁止对齐优化

// 函数声明
void idt_init();             // 初始化 IDT
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);  // 设置 IDT 条目

#endif
```

---

### 📂 3. IDT 实现：`kernel/idt.c`

```c
#include "idt.h"             // 引入 IDT 结构定义
#include "io.h"              // 引入 inb/outb 函数（虽然这里没用到，但保持一致性）

// 定义 IDT 数组（256 个条目）
// 每个条目 8 字节，总共 2048 字节
struct idt_entry idt_entries[256];

// 定义 IDT 指针（6 字节）
// 用于告诉 CPU IDT 的位置
struct idt_ptr idtp;

// 外部声明汇编中断处理程序
// 这个函数在 irq.asm 中定义
extern void irq1_handler();

// 初始化 IDT
void idt_init() {
	// 设置 IDT 指针
	// limit = 256 * 8 - 1 = 2047（IDT 的总字节数减 1）
	idtp.limit = sizeof(struct idt_entry) * 256 - 1;
	// base = IDT 数组的起始地址
	idtp.base = (uint32_t)&idt_entries;

	// 清空 IDT（把所有条目设为 0）
	for (int i = 0; i < 256; i++) {
		idt_set_gate(i, 0, 0, 0);
	}

	// 设置 IRQ1 (键盘) 的中断门
	// 参数说明：
	//   33 = 中断号（IRQ1 重映射后是 33）
	//   (uint32_t)irq1_handler = 处理程序地址
	//   0x08 = 代码段选择子（指向 GDT 中的代码段）
	//   0x8E = 属性（32 位中断门，Ring 0，Present=1）
	idt_set_gate(33, (uint32_t)irq1_handler, 0x08, 0x8E);

	// 使用 lidt 指令加载 IDT
	// 这条指令告诉 CPU："以后发生中断时，去这个地址查表"
	__asm__ volatile("lidt (%0)" : : "r"(&idtp));
}

// 设置 IDT 条目
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
	// 把处理程序地址拆分成低 16 位和高 16 位
	idt_entries[num].base_lo = (base & 0xFFFF);           // 低 16 位
	idt_entries[num].base_hi = (base >> 16) & 0xFFFF;     // 高 16 位
	// 设置段选择子（指向代码段）
	idt_entries[num].sel = sel;
	// 必须为 0
	idt_entries[num].always0 = 0;
	// 设置属性（类型、DPL、Present 等）
	idt_entries[num].flags = flags;
}
```

---

### 📂 4. PIC 重映射：`kernel/pic.c`

```c
#include "io.h"              // 引入 inb/outb 函数
#include "types.h"           // 引入类型定义

// PIC 端口定义
// 8259A PIC 有两个：主 PIC 和从 PIC
// 每个 PIC 有两个端口：命令端口和数据端口
#define PIC1_CMD 0x20        // 主 PIC 命令端口
#define PIC1_DATA 0x21       // 主 PIC 数据端口
#define PIC2_CMD 0xA0        // 从 PIC 命令端口
#define PIC2_DATA 0xA1       // 从 PIC 数据端口

// 重新映射 PIC，把 IRQ0-IRQ15 映射到中断号 0x20-0x2F
void pic_remap() {
	// ICW1 (Initialization Command Word 1)
	// 0x11 = 00010001
	//   bit 4 = 1: 需要 ICW4
	//   bit 1 = 1: 级联模式（主从 PIC）
	outb(PIC1_CMD, 0x11);   // 初始化主 PIC
	outb(PIC2_CMD, 0x11);   // 初始化从 PIC

	// ICW2: 设置中断号偏移
	// 主 PIC 的 IRQ0-IRQ7 映射到 0x20-0x27（32-39）
	outb(PIC1_DATA, 0x20);
	// 从 PIC 的 IRQ8-IRQ15 映射到 0x28-0x2F（40-47）
	outb(PIC2_DATA, 0x28);

	// ICW3: 设置级联关系
	// 主 PIC 的 IRQ2 连接从 PIC
	outb(PIC1_DATA, 0x04);  // 00000100 = IRQ2
	// 从 PIC 连接到主 PIC 的 IRQ2
	outb(PIC2_DATA, 0x02);  // 00000010 = IRQ2

	// ICW4: 设置 8086 模式
	// 0x01 = 8086/8088 模式（而不是 8080/8085 模式）
	outb(PIC1_DATA, 0x01);
	outb(PIC2_DATA, 0x01);

	// 设置中断屏蔽掩码
	// 0xFD = 11111101
	//   bit 0 = 1: 屏蔽 IRQ0（定时器）
	//   bit 1 = 0: 开放 IRQ1（键盘）
	//   bit 2-7 = 1: 屏蔽其他中断
	// 【关键】：只开放 IRQ1，因为其他中断我们没有处理程序
	outb(PIC1_DATA, 0xFD);
	// 屏蔽从 PIC 的所有中断
	outb(PIC2_DATA, 0xFF);
}
```

---

### 📂 5. 汇编中断处理程序：`kernel/irq.asm`

```nasm
[BITS 32]                    ; 32 位保护模式代码
section .text                ; 代码段

extern keyboard_handler      ; 声明外部 C 函数（在 keyboard.c 中定义）

; IRQ1 (键盘) 中断处理程序
; 当键盘中断发生时，CPU 会自动跳转到这里
global irq1_handler          ; 导出符号，让链接器能找到
irq1_handler:
	pusha                   ; 保存所有通用寄存器（EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP）
	                        ; 这是为了保护现场，防止中断处理破坏主程序的寄存器
    
	call keyboard_handler   ; 调用 C 语言处理函数
	                        ; 这个函数会读取扫描码，转换为 ASCII，输出到串口和 VGA
    
	popa                    ; 恢复所有通用寄存器
	                        ; 恢复到中断发生前的状态
    
	; 发送 EOI (End of Interrupt) 给 PIC
	; 告诉 PIC "中断处理完成了，可以接受下一个中断了"
	mov al, 0x20            ; EOI 命令码
	out 0x20, al            ; 发送到主 PIC 的命令端口
    
	iret                    ; 中断返回指令
	                        ; 恢复 EFLAGS、CS、EIP，回到被中断的代码继续执行
```

---

### 📂 6. 键盘处理函数：`kernel/keyboard.c`

```c
#include "io.h"              // 引入 inb/outb 函数
#include "serial.h"          // 引入串口输出函数
#include "vga.h"             // 引入 VGA 输出函数
#include "types.h"           // 引入类型定义

// 键盘状态变量
static int shift_pressed = 0;    // Shift 键是否按下（0=未按下，1=按下）
static int caps_lock_on = 0;     // Caps Lock 是否开启（0=关闭，1=开启）

// 普通状态映射表（小写字母）
// 索引是扫描码，值是 ASCII 字符
// 例如：map_normal[0x1E] = 'a'（扫描码 0x1E 对应字母 a）
static char map_normal[128] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',  // 0x00-0x0E
	'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',  // 0x0F-0x1C
	0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',          // 0x1D-0x29
	0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,            // 0x2A-0x36
	'*', 0, ' ', 0,                                                              // 0x37-0x3A
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // 0x3B-0x53
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   // 0x54-0x6B
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0                 // 0x6C-0x7F
};

// Shift 状态映射表（大写字母和符号）
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

// 键盘中断处理函数
// 当键盘中断发生时，这个函数会被调用
void keyboard_handler() {
	// 从端口 0x60 读取扫描码
	// 扫描码是键盘硬件发送的原始信号
	// 例如：按下 'a' 键，扫描码是 0x1E
	uint8_t scancode = inb(0x60);

	// 判断是按下还是释放
	// 扫描码的最高位（bit 7）：
	//   0 = 按下
	//   1 = 释放
	int is_release = (scancode & 0x80);
	// 去掉最高位，得到真正的扫描码
	uint8_t key = scancode & 0x7F;

	// 左 Shift (0x2A) 和 右 Shift (0x36)
	// 按下时设置 shift_pressed = 1，释放时设置为 0
	if (key == 0x2A || key == 0x36) {
		shift_pressed = !is_release;  // 按下=1，释放=0
		return;                        // 不输出字符，直接返回
	}

	// Caps Lock (0x3A) - 只在按下时切换状态
	if (key == 0x3A && !is_release) {
		// 切换 Caps Lock 状态
		caps_lock_on = !caps_lock_on;
		// 在 VGA 屏幕上显示状态
		if (caps_lock_on) {
			vga_puts("\n[CAPS ON]\n");
		} else {
			vga_puts("\n[CAPS OFF]\n");
		}
		return;  // 不输出字符，直接返回
	}

	// 忽略所有释放事件
	// 我们只在按下时输出字符，避免重复输入
	if (is_release) {
		return;
	}

	// 确定是否大写
	// 使用异或逻辑：
	//   Shift=0, Caps=0 -> 小写 (0^0=0)
	//   Shift=1, Caps=0 -> 大写 (1^0=1)
	//   Shift=0, Caps=1 -> 大写 (0^1=1)
	//   Shift=1, Caps=1 -> 小写 (1^1=0)  ← 和真实键盘一样！
	int is_upper = shift_pressed ^ caps_lock_on;

	// 根据状态查表
	char c;
	if (is_upper) {
		c = map_shift[key];      // 大写映射表
	} else {
		c = map_normal[key];     // 小写映射表
	}

	// 如果查到了字符（不是 0）
	if (c != 0) {
		// 同时输出到串口和 VGA 屏幕
		serial_putc(c);          // 串口输出（在 Linux 终端显示）
		vga_putc(c);             // VGA 输出（在 QEMU 屏幕显示）
	}
}
```

---

### 📂 7. VGA 驱动：`kernel/vga.c`

```c
#include "io.h"              // 引入 inb/outb 函数（必须包含，因为用到了 outb）
#include "vga.h"             // 引入 VGA 函数声明

// VGA 文本模式显存的物理起始地址
// 在 PC 架构中，内存的 0xb8000 到 0xbffff 被硬连线到了显卡的显存
// 你往这个地址写数据，屏幕上就会立刻显示出来
#define VIDEO_MEMORY 0xb8000

// VGA 文本模式的分辨率
#define VGA_WIDTH 80         // 80 列
#define VGA_HEIGHT 25        // 25 行

// 光标位置（全局变量）
static int cursor_x = 0;     // 当前光标的 X 坐标（0-79）
static int cursor_y = 0;     // 当前光标的 Y 坐标（0-24）

// 颜色属性：黑底高亮白字
// 高 4 位 = 背景色（0=黑色）
// 低 4 位 = 前景色（0xF=高亮白色）
static const uint8_t color = 0x0F;

// 初始化 VGA
void vga_init() {
	vga_clear();            // 清屏
}

// 清屏
void vga_clear() {
	char* vidptr = (char*)VIDEO_MEMORY;  // 指向显存
    
	// 遍历整个屏幕（80列 x 25行 = 2000 个字符）
	// 每个字符占 2 个字节：字符 + 颜色属性
	for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT * 2; i += 2) {
		vidptr[i] = ' ';       // 写入空格字符
		vidptr[i + 1] = 0x07;  // 写入颜色属性（黑底白字）
	}
    
	// 重置光标位置到左上角
	cursor_x = 0;
	cursor_y = 0;
}

// 更新硬件光标位置
// VGA 控制器有一个硬件光标，我们需要告诉它当前光标在哪里
static void vga_update_cursor() {
	// 计算光标在屏幕上的位置（0-1999）
	uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    
	// 设置光标位置低字节
	// 0x3D4 是 VGA 控制器的索引端口
	// 0x0F 是光标位置低字节的索引
	outb(0x3D4, 0x0F);
	// 0x3D5 是 VGA 控制器的数据端口
	outb(0x3D5, (uint8_t)(pos & 0xFF));  // 写入低 8 位
    
	// 设置光标位置高字节
	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));  // 写入高 8 位
}

// 滚动屏幕
// 当光标到达屏幕底部时，把所有行上移一行
static void vga_scroll() {
	char* vidptr = (char*)VIDEO_MEMORY;
    
	// 把第 1-24 行上移到第 0-23 行
	for (int y = 1; y < VGA_HEIGHT; y++) {
		for (int x = 0; x < VGA_WIDTH; x++) {
			int src = (y * VGA_WIDTH + x) * 2;      // 源位置
			int dst = ((y - 1) * VGA_WIDTH + x) * 2;  // 目标位置
			vidptr[dst] = vidptr[src];              // 复制字符
			vidptr[dst + 1] = vidptr[src + 1];      // 复制颜色
		}
	}
    
	// 清空最后一行
	for (int x = 0; x < VGA_WIDTH; x++) {
		int pos = ((VGA_HEIGHT - 1) * VGA_WIDTH + x) * 2;
		vidptr[pos] = ' ';
		vidptr[pos + 1] = 0x07;
	}
    
	// 把光标移到最后一行
	cursor_y = VGA_HEIGHT - 1;
}

// 在屏幕上打印一个字符
void vga_putc(char c) {
	char* vidptr = (char*)VIDEO_MEMORY;
    
	if (c == '\n') {
		// 换行：光标回到行首，下移一行
		cursor_x = 0;
		cursor_y++;
	} else if (c == '\r') {
		// 回车：光标回到行首
		cursor_x = 0;
	} else if (c == '\b') {
		// 退格：光标左移一格，用空格覆盖
		if (cursor_x > 0) {
			cursor_x--;
			int pos = (cursor_y * VGA_WIDTH + cursor_x) * 2;
			vidptr[pos] = ' ';
			vidptr[pos + 1] = 0x07;
		}
	} else {
		// 普通字符：写入显存
		int pos = (cursor_y * VGA_WIDTH + cursor_x) * 2;
		vidptr[pos] = c;           // 写入字符
		vidptr[pos + 1] = color;   // 写入颜色属性
		cursor_x++;
        
		// 如果到达行尾，换行
		if (cursor_x >= VGA_WIDTH) {
			cursor_x = 0;
			cursor_y++;
		}
	}
    
	// 如果到达屏幕底部，滚动
	if (cursor_y >= VGA_HEIGHT) {
		vga_scroll();
	}
    
	// 更新硬件光标位置
	vga_update_cursor();
}

// 在屏幕上打印字符串
void vga_puts(const char* str) {
	while (*str) {           // 遍历字符串，直到遇到 '\0'
		vga_putc(*str);      // 逐个字符打印
		str++;               // 指针后移
	}
}
```

---

### 📂 8. 内核主函数：`kernel/kernel.c`

```c
#include "serial.h"          // 引入串口函数
#include "vga.h"             // 引入 VGA 函数
#include "idt.h"             // 引入 IDT 函数
#include "types.h"           // 引入类型定义

// 外部声明 PIC 重映射函数（在 pic.c 中定义）
extern void pic_remap();

// 内核主函数
// 这是 C 语言的入口点，由 entry.asm 调用
void kernel_main() {
	// 初始化串口
	serial_init();
	serial_puts("\n\r[Kernel] Initializing...\n\r");

	// 初始化 VGA（清屏）
	vga_init();
	vga_puts("OS Ready! Type something:\n");

	// 初始化 IDT（设置中断向量表）
	idt_init();

	// 重新映射 PIC（避免与 CPU 异常冲突）
	pic_remap();

	// 开启中断（sti 指令）
	// 这条指令告诉 CPU："现在可以接受中断了"
	__asm__ volatile("sti");

	serial_puts("[Kernel] System ready!\n\r");

	// 死循环，等待中断
	// hlt 指令让 CPU 停止执行，进入低功耗状态
	// 当有中断发生时，CPU 会被唤醒，执行中断处理程序
	// 处理完成后，CPU 回到这里，继续 hlt
	while(1) {
		__asm__ volatile("hlt");
	}
}
```

---

## 🐛 错误排查完整记录

### 1. 链接错误：undefined reference to `pic_remap`

**错误信息：**
```text
kernel/kernel.o: in function `kernel_main':
kernel.c:(.text+0x31): undefined reference to `pic_remap'
```

**原因：** Makefile 的 `KERNEL_OBJS` 漏掉了 `pic.o`。

**解决：** 在 Makefile 中添加 `$(KERNEL_DIR)/pic.o`。

**教训：** 添加新文件后，务必更新 Makefile！

---

### 2. 屏幕闪烁（无限重启）

**现象：** 系统运行到 `[8] VGA done` 后立刻重启。

**原因：** PIC 开放了 IRQ0 (定时器)，但没有编写 IRQ0 的处理程序。定时器每秒触发 18.2 次，CPU 找不到处理程序，触发 Triple Fault 重启。

**解决：** 修改 `pic.c`，将 `PIC1_DATA` 设为 `0xFD` (只开放 IRQ1 键盘，屏蔽 IRQ0)。

**教训：** 开启中断前，必须确保所有开放的中断都有对应的处理程序！

---

### 3. 编译错误：implicit declaration of function 'outb'

**错误信息：**
```text
kernel/vga.c: In function 'vga_update_cursor':
kernel/vga.c:33:5: error: implicit declaration of function 'outb'
```

**原因：** `vga.c` 中使用了 `outb`，但忘记包含 `io.h`。

**解决：** 在 `vga.c` 第一行添加 `#include "io.h"`。

**教训：** 使用硬件端口操作时，必须包含 `io.h`。

---

### 4. 键盘映射错位（按 Z 显示 X）

**原因：** 扫描码映射表索引错位，未严格遵循 PS/2 Set 1 标准。

**解决：** 使用正确的 128 字节标准映射表。

---

### 5. 按住按键重复输入

**原因：** 未区分"按下"和"释放"事件。

**解决：** 检查 `scancode & 0x80`，如果是释放事件则直接 `return`。

---

## 🎯 核心知识点总结

1. **中断流程**：硬件触发 -> PIC 转发 -> CPU 查 IDT -> 汇编保存现场 (`pusha`) -> 调用 C 函数 -> 恢复现场 (`popa`) -> 发送 EOI -> `iret` 返回。

2. **关键端口**：
   - `0x60` - 键盘数据端口
   - `0x20/0x21` - 主 PIC 命令/数据端口
   - `0x3F8` - COM1 串口数据端口
   - `0x3D4/0x3D5` - VGA 光标位置索引/数据端口
   - `0xb8000` - VGA 文本模式显存起始地址

3. **状态机设计**：通过 `shift_pressed` 和 `caps_lock_on` 的异或 (`^`) 逻辑，完美模拟真实键盘的大小写切换行为。

---

**恭喜你完成了操作系统开发中最难、最核心的部分之一！** 🎊
