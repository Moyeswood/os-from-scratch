---
tags:
  - OSDev
  - Interrupts
  - IDT
  - PIC
  - Keyboard
date: 2026-10-04
---

# ⚡ 让内核"活"过来：中断系统与键盘驱动

> [!abstract] 本关目标
> 配置 IDT 和 8259A PIC，接管键盘中断 (IRQ1)，实现实时按键回显。

---

## 🧠 核心概念：什么是中断？

### 比喻：中断就像"门铃"

- **没有中断时**：你必须每隔一秒去门口看看有没有人找你（**轮询**）。极其浪费 CPU。
- **有中断时**：你装了一个门铃。有人来时，门铃响（**硬件发出中断信号**），你暂停工作去开门（**CPU 跳转到中断处理程序**）。

**中断的本质：硬件主动通知 CPU "有事发生"，CPU 暂停当前任务，去执行预设的处理程序。**

---

## 🏗️ 中断系统的三大组件

### 1️⃣ IDT (Interrupt Descriptor Table) - 中断描述符表
**作用**：告诉 CPU "当 N 号中断发生时，去内存的哪个地址执行处理程序"。

类比：一本**电话簿**。
- 中断号 32 (键盘) -> 处理程序地址 K

### 2️⃣ PIC (Programmable Interrupt Controller) - 可编程中断控制器
**作用**：管理硬件中断信号。8259A PIC 有 16 个中断引脚（IRQ0-IRQ15）。

**问题**：PIC 默认把 IRQ0-IRQ7 映射到 CPU 的中断号 0x08-0x0F，但这些被 CPU 内部异常占用了！

**解决**：重新映射 PIC，把 IRQ0-IRQ15 移到中断号 0x20-0x2F（32-47）。

### 3️⃣ 中断处理程序 (ISR/IRQ Handler)
**作用**：当特定中断发生时，CPU 自动跳转执行的函数。

---

## 📂 1. 类型定义：`kernel/types.h`

```c
#ifndef TYPES_H
#define TYPES_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

#endif
```

---

## 📂 2. IDT 结构定义：`kernel/idt.h`

```c
#ifndef IDT_H
#define IDT_H

#include "types.h"

// IDT 条目结构（8 字节）
struct idt_entry {
	uint16_t base_lo;  // 处理程序地址的低 16 位
	uint16_t sel;      // 内核段选择子（我们用 0x08）
	uint8_t always0;   // 必须为 0
	uint8_t flags;     // 类型和属性（0x8E = 32 位中断门）
	uint16_t base_hi;  // 处理程序地址的高 16 位
} __attribute__((packed));

// IDT 指针结构（6 字节）
struct idt_ptr {
	uint16_t limit;  // IDT 的大小（字节数 - 1）
	uint32_t base;   // IDT 的起始地址
} __attribute__((packed));

// 初始化 IDT
void idt_init();

// 设置 IDT 条目
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

#endif
```

---

## 📂 3. IDT 实现：`kernel/idt.c`

```c
#include "idt.h"
#include "io.h"

// 声明 IDT 数组（256 个条目）
struct idt_entry idt_entries[256];
struct idt_ptr idtp;

// 外部声明汇编中断处理程序
extern void irq1_handler();

// 初始化 IDT
void idt_init() {
	idtp.limit = sizeof(struct idt_entry) * 256 - 1;
	idtp.base = (uint32_t)&idt_entries;

	// 清空 IDT
	for (int i = 0; i < 256; i++) {
		idt_set_gate(i, 0, 0, 0);
	}

	// 加载 IDT（使用 lidt 指令）
	__asm__ volatile("lidt (%0)" : : "r"(&idtp));
}

// 设置 IDT 条目
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
	idt_entries[num].base_lo = (base & 0xFFFF);
	idt_entries[num].base_hi = (base >> 16) & 0xFFFF;
	idt_entries[num].sel = sel;
	idt_entries[num].always0 = 0;
	idt_entries[num].flags = flags;
}
```

---

## 📂 4. PIC 重映射：`kernel/pic.c`

```c
#include "io.h"
#include "types.h"

// PIC 端口定义
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1

// 重新映射 PIC，把 IRQ0-IRQ15 映射到中断号 0x20-0x2F
void pic_remap() {
	// 初始化命令：ICW1
	outb(PIC1_CMD, 0x11);  // 级联，需要 ICW4
	outb(PIC2_CMD, 0x11);

	// ICW2：设置中断号偏移
	outb(PIC1_DATA, 0x20);  // IRQ0-IRQ7 -> 0x20-0x27
	outb(PIC2_DATA, 0x28);  // IRQ8-IRQ15 -> 0x28-0x2F

	// ICW3：级联关系
	outb(PIC1_DATA, 0x04);  // 主 PIC 的 IRQ2 连接从 PIC
	outb(PIC2_DATA, 0x02);

	// ICW4：设置 8086 模式
	outb(PIC1_DATA, 0x01);
	outb(PIC2_DATA, 0x01);

	// 屏蔽所有中断（除了 IRQ1 键盘）
	outb(PIC1_DATA, 0xFC);  // 11111100 - 只开放 IRQ0 和 IRQ1
	outb(PIC2_DATA, 0xFF);  // 屏蔽从 PIC 的所有中断
}
```

---

## 📂 5. 汇编中断处理程序：`kernel/irq.asm`

```nasm
[BITS 32]
section .text

extern keyboard_handler  ; 声明 C 语言处理函数

; IRQ1 (键盘) 中断处理程序
global irq1_handler
irq1_handler:
	pusha               ; 保存所有通用寄存器
	call keyboard_handler ; 调用 C 语言处理函数
	popa                ; 恢复寄存器
    
	; 发送 EOI (End of Interrupt) 给 PIC
	mov al, 0x20
	out 0x20, al
    
	iret                ; 中断返回
```

---

## 📂 6. 键盘处理函数：`kernel/keyboard.c`

```c
#include "io.h"
#include "serial.h"

// 简单的扫描码到 ASCII 映射表（仅支持字母和数字）
char scancode_to_ascii(uint8_t scancode) {
	static char map[] = {
		0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
		'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
		'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
		'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 0, 0, ' '
	};
    
	if (scancode < sizeof(map)) {
		return map[scancode];
	}
	return 0;
}

// 键盘中断处理函数
void keyboard_handler() {
	uint8_t scancode = inb(0x60);  // 从端口 0x60 读取扫描码
    
	char c = scancode_to_ascii(scancode);
	if (c != 0) {
		serial_putc(c);  // 通过串口打印字符
	}
}
```

---

## 📂 7. 更新内核主函数：`kernel/kernel.c`

```c
#include "serial.h"
#include "idt.h"
#include "types.h"

// 外部声明 PIC 重映射函数
extern void pic_remap();

#define VIDEO_MEMORY 0xb8000

void kernel_main() {
	serial_init();
	serial_puts("\n\r[Kernel] Initializing IDT...\n\r");
    
	// 初始化 IDT
	idt_init();
    
	serial_puts("[Kernel] Remapping PIC...\n\r");
    
	// 重新映射 PIC
	pic_remap();
    
	serial_puts("[Kernel] Enabling interrupts...\n\r");
    
	// 开启中断（sti 指令）
	__asm__ volatile("sti");
    
	serial_puts("[Kernel] System ready! Press keys...\n\r");

	// VGA 显示
	char* vidptr = (char*)VIDEO_MEMORY;
	const char* str = "Press keys in QEMU!";
	int i = 0;
    
	for (int j = 0; j < 80 * 25 * 2; j += 2) {
		vidptr[j] = ' ';
		vidptr[j + 1] = 0x07;
	}

	while(str[i] != '\0') {
		vidptr[i * 2]     = str[i];
		vidptr[i * 2 + 1] = 0x0F;
		i++;
	}

	// 死循环，等待中断
	while(1) {
		__asm__ volatile("hlt"); 
	}
}
```

---

## 📂 8. 更新 Makefile

```makefile
ASM = nasm
CC = gcc
LD = ld
OBJCOPY = objcopy
QEMU = qemu-system-i386

BOOT_DIR = boot
KERNEL_DIR = kernel
BUILD_DIR = build

BOOT_BIN = $(BUILD_DIR)/boot.bin
KERNEL_ELF = $(BUILD_DIR)/kernel.elf
KERNEL_BIN = $(BUILD_DIR)/kernel.bin
OS_IMG = $(BUILD_DIR)/os.img

# 【新增】添加 idt.o, irq.o, keyboard.o
KERNEL_OBJS = $(KERNEL_DIR)/entry.o $(KERNEL_DIR)/kernel.o $(KERNEL_DIR)/serial.o $(KERNEL_DIR)/idt.o $(KERNEL_DIR)/irq.o $(KERNEL_DIR)/keyboard.o

all: $(OS_IMG)

$(BOOT_BIN): $(BOOT_DIR)/boot.asm
	mkdir -p $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

$(KERNEL_DIR)/entry.o: $(KERNEL_DIR)/entry.asm
	$(ASM) -f elf32 $< -o $@

# 【新增】编译 irq.asm
$(KERNEL_DIR)/irq.o: $(KERNEL_DIR)/irq.asm
	$(ASM) -f elf32 $< -o $@

$(KERNEL_DIR)/%.o: $(KERNEL_DIR)/%.c
	$(CC) -m32 -ffreestanding -fno-pie -nostdlib -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS)
	$(LD) -m elf_i386 -T $(KERNEL_DIR)/linker.ld $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(OS_IMG): $(BOOT_BIN) $(KERNEL_BIN)
	dd if=/dev/zero of=$@ bs=1024 count=1440 status=none
	dd if=$(BOOT_BIN) of=$@ conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=1 conv=notrunc status=none

run: $(OS_IMG)
	$(QEMU) -fda $< -serial stdio

debug: $(OS_IMG)
	$(QEMU) -fda $< -s -S -serial stdio

clean:
	rm -rf $(BUILD_DIR) $(KERNEL_DIR)/*.o

.PHONY: all run debug clean
```

---

## 🚀 运行与测试

1. 编译：`make clean && make`
2. 运行：`make run`
3. 在 QEMU 窗口中按键盘，观察 Linux 终端的输出！

---

## 🎯 核心知识点总结

1. **IDT**：中断向量表，存储 256 个中断处理程序的地址。
2. **PIC 重映射**：避免硬件中断与 CPU 异常冲突。
3. **汇编跳板**：保存/恢复寄存器，调用 C 函数，发送 EOI。
4. **键盘扫描码**：从端口 0x60 读取，转换为 ASCII。
