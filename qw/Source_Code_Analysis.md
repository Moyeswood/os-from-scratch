---
tags:
  - OSDev
  - CodeReview
  - x86
  - Assembly
  - C_Language
date: 2026-10-04
---
/
# 🔬 x86 操作系统核心源码逐行解剖

> [!abstract] 阅读指南
> 本文档对操作系统的 6 个核心源文件进行了逐行拆解。
> 重点不在于语法，而在于每一行代码背后，**CPU 寄存器、内存地址和硬件端口**到底发生了什么。

---

## 📂 1. 引导扇区：`boot/boot.asm`

> [!info] 核心使命
> 在 CPU 最古老的 16 位实模式下，完成硬件初始化，读取内核，并强行把 CPU 踹进 32 位保护模式。

```nasm
[BITS 16]       ; 告诉 NASM：接下来的代码是 16 位指令。CPU 刚加电时就是 16 位模式。
[ORG 0x7C00]    ; ORG：告诉编译器，这段代码会被 BIOS 加载到内存的 0x7C00 地址。

; ================= 1. 初始化段寄存器 =================
xor ax, ax      ; 将 AX 寄存器清零 (自己异或自己是最高效的清零方式)。
mov ds, ax      ; 将数据段寄存器 DS 设为 0。
mov es, ax      ; 将附加段寄存器 ES 设为 0。
mov ss, ax      ; 将栈段寄存器 SS 设为 0。
mov sp, 0x7C00  ; 将栈指针 SP 设为 0x7C00。
                ; 【原理】：栈向下生长。把栈顶设在 0x7C00，完美避开了代码所在的 0x7C00~0x7DFF 区域。

; ================= 2. 开启 A20 地址线 =================
in al, 0x92     ; 从端口 0x92 (系统控制端口A) 读取一个字节到 AL。
or al, 2        ; 将 AL 的第 1 位 (二进制 00000010) 置为 1。
out 0x92, al    ; 将修改后的 AL 写回端口 0x92。
                ; 【原理】：解除 1MB 内存寻址限制，允许访问 1MB 以上的内存。

; ================= 3. 从磁盘读取内核 =================
mov ah, 0x02    ; BIOS 中断 0x13 的子功能号：0x02 代表"读扇区"。
mov al, 10      ; 读取 10 个扇区 (10 * 512 = 5120 字节)，防止内核大于 512 字节。
mov ch, 0       ; 柱面号：0。
mov cl, 2       ; 扇区号：2 (扇区 1 是引导扇区，内核从扇区 2 开始)。
mov dh, 0       ; 磁头号：0。
mov bx, 0x1000  ; 目标内存的段地址：0x1000。
mov es, bx      ; 将 ES 设为 0x1000。
xor bx, bx      ; 目标内存的偏移地址：0x0000 (BX 清零)。
                ; 【原理】：ES:BX 组合，物理地址 = 0x1000 * 16 + 0 = 0x10000。
int 0x13        ; 触发 BIOS 的 13 号中断，执行读盘操作。
jc disk_error   ; JC：如果读盘失败，CF 标志位为 1，跳转到 disk_error。

; ================= 4. 加载 GDT 并开启保护模式 =================
cli             ; 关闭中断。切换 CPU 模式时绝不能被打断。
lgdt [gdt_desc] ; 将 gdt_desc 指向的内存数据，加载到 CPU 的 GDTR 寄存器中。

mov eax, cr0    ; 将控制寄存器 CR0 的值读到 EAX。
or eax, 1       ; 将 EAX 的最低位 (PE 位，Protection Enable) 置为 1。
mov cr0, eax    ; 将修改后的 EAX 写回 CR0。
                ; 【高光时刻】：CPU 硬件电路翻转，正式跨入 32 位保护模式！

jmp dword 0x08:pmode ; 远跳转。0x08 是代码段选择子。
                ; 【致命细节】：必须加 dword！强制 CPU 指令解码器切换到 32 位模式。

; ================= 读盘错误处理 =================
disk_error:
	mov ah, 0x0E    ; BIOS 0x10 中断：打印字符。
	mov al, 'E'     ; 打印 'E' (Error)。
	int 0x10
	jmp $           ; 死循环，防止 CPU 执行乱码。

; ================= 32 位保护模式代码 =================
[BITS 32]
pmode:
	mov ax, 0x10    ; 0x10 是数据段选择子。
	mov ds, ax      ; 初始化所有数据相关的段寄存器，否则访问内存会触发 GPF 异常。
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	mov esp, 0x90000 ; 设置 32 位下的栈顶指针 (ESP)。

	jmp 0x10000     ; 跳转到内存的 0x10000 处，即 C 语言内核的入口！

; ================= GDT (全局描述符表) 定义 =================
gdt_start:
	dq 0            ; 第 0 个描述符必须是 0 (Intel 硬性规定)。
gdt_code:
	dw 0xFFFF       ; 段界限 0-15 位。
	dw 0x0000       ; 基地址 0-15 位。
	db 0x00         ; 基地址 16-23 位。
	db 10011010b    ; 访问权限：Present=1, DPL=0(Ring 0), 代码段, 可执行/可读。
	db 11001111b    ; 标志：Granularity=1(4KB粒度), D/B=1(32位), 界限高4位=F。
	db 0x00         ; 基地址 24-31 位。
gdt_data:
	dw 0xFFFF
	dw 0x0000
	db 0x00
	db 10010010b    ; 访问权限：数据段, 可读写。
	db 11001111b
	db 0x00
gdt_end:

gdt_desc:
	dw gdt_end - gdt_start - 1 ; GDT 表的总长度 (字节数减 1)。
	dd gdt_start               ; GDT 表在内存中的物理起始地址。

times 510 - ($ - $$) db 0 ; 用 0 填充，直到当前偏移量达到 510 字节。
dw 0xAA55                 ; 引导扇区的魔数。BIOS 靠它判断是否为合法引导程序。
```

---

## 📂 2. 汇编跳板：`kernel/entry.asm`

> [!question] 核心使命
> 作为 C 语言的"接生婆"，确保 CPU 准确无误地跳入 C 语言的 `kernel_main` 函数。

```nasm
[BITS 32]
section .text.start     ; 【核心魔法】：告诉链接器，把这段代码放在 .text.start 段。
                        ; 配合 linker.ld，强制这段代码排在所有代码的最前面。

global _start           ; 声明为全局符号，让链接器能看见。
extern kernel_main      ; 告诉汇编器：kernel_main 在别的文件 (C语言) 里定义。

_start:                 ; 内核被加载到 0x10000 后，CPU 执行的第一条指令。
	call kernel_main   ; 调用 C 语言的主函数。
	jmp $              ; 如果 kernel_main 居然 return 了，就死循环。内核绝不能退出。
```

---

## 📂 3. 链接脚本：`kernel/linker.ld`

> [!tip] 核心使命
> 像上帝一样，规定内核代码在内存中的绝对物理布局。

```ld
SECTIONS {
	. = 0x10000;        ; 【核心】：位置计数器设为 0x10000。
	                    ; 接下来的所有代码，物理地址都将从 0x10000 开始计算。
    
	.text : {
		*(.text.start) ; 【核心】：首先，把 .text.start 段 (entry.asm) 放在最前面！
		*(.text)       ; 然后，把其他所有的 .text 段 (kernel.c 等) 放在后面。
	}
    
	.rodata : { *(.rodata) } ; 只读数据段 (如 const 字符串)。
	.data : { *(.data) }     ; 已初始化的全局变量。
	.bss : { *(.bss) }       ; 未初始化的全局变量 (会自动清零)。
}
```

---

## 📂 4. 底层 I/O 头文件：`kernel/io.h`

> [!abstract] 核心使命
> 提供 C 语言操作 CPU 硬件端口的"桥梁"（内联汇编）。

```c
#ifndef IO_H
#define IO_H

// 从指定的硬件端口读取一个字节 (8位)。
static inline unsigned char inb(unsigned short port) {
	unsigned char ret;
	// 【核心】：GCC 内联汇编。"inb %1, %0" 是模板。
	// "=a"(ret) : 结果存入 EAX 的低 8 位 (AL)，并赋值给 ret。
	// "Nd"(port) : 变量 port 的值放入 DX 寄存器。
	__asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

// 向指定的硬件端口写入一个字节。
static inline void outb(unsigned short port, unsigned char val) {
	// "outb %0, %1" : 把 AL (val) 输出到 DX (port) 指定的端口。
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

#endif
```

---

## 📂 5. 串口驱动：`kernel/serial.c`

> [!example] 核心使命
> 配置 UART 串口芯片 (COM1, 端口 0x3F8)，把日志打印到宿主机终端。

```c
#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init() {
	outb(COM1 + 1, 0x00); // 禁用串口中断 (目前用轮询方式)。
	outb(COM1 + 3, 0x80); // 启用 DLAB，为了设置波特率。
	outb(COM1 + 0, 0x03); // 波特率除数低字节 (3 = 115200 / 38400)。
	outb(COM1 + 1, 0x00); // 波特率除数高字节。
	outb(COM1 + 3, 0x03); // 禁用 DLAB，设置 8 位数据，无校验，1 停止位。
	outb(COM1 + 2, 0xC7); // 启用 FIFO，清空缓冲，14 字节触发阈值。
	outb(COM1 + 4, 0x0B); // IRQ 启用，RTS/DSR 引脚状态。
}

static int serial_is_transmit_fifo_empty() {
	// 读取线路状态寄存器 (COM1 + 5)。第 5 位 (0x20) 为 1 说明缓冲区空了。
	return inb(COM1 + 5) & 0x20;
}

void serial_putc(char c) {
	while (serial_is_transmit_fifo_empty() == 0); // 死循环等待缓冲区空。
	outb(COM1, c); // 写入数据寄存器，硬件自动发送。
}

void serial_puts(const char* str) {
	while (*str) {
		serial_putc(*str);
		str++;
	}
}
```

---

## 📂 6. C 语言内核：`kernel/kernel.c`

> [!success] 核心使命
> 操作系统的"大脑"，直接操作 VGA 显存，并进入空闲循环。

```c
#include "serial.h"

// 【核心原理】：VGA 文本模式显存的物理起始地址。
// 内存的 0xb8000 被硬连线到显卡显存，写数据就会立刻显示。
#define VIDEO_MEMORY 0xb8000

void kernel_main() {
	serial_init();
	serial_puts("\n\r[Kernel] Serial port initialized!\n\r");

	char* vidptr = (char*)VIDEO_MEMORY;
	const char* str = "Hello from 32-bit C Kernel!";
	int i = 0;
    
	// 清屏操作。VGA 文本模式 80列 x 25行，每字符 2 字节。
	for (int j = 0; j < 80 * 25 * 2; j += 2) {
		vidptr[j] = ' ';       // 写入空格。
		vidptr[j + 1] = 0x07;  // 颜色属性：0x07 = 黑底白字。
	}

	// 打印字符串。
	while(str[i] != '\0') {
		vidptr[i * 2]     = str[i];  // 写入字符。
		vidptr[i * 2 + 1] = 0x0F;    // 颜色属性：0x0F = 黑底高亮白字。
		i++;
	}
    
	serial_puts("[Kernel] Entering idle loop...\n\r");

	// 操作系统空闲循环 (Idle Loop)。
	while(1) {
		// 【核心】：hlt 指令让 CPU 停止执行，进入低功耗状态，直到有外部中断唤醒。
		// volatile 告诉 gcc：不要优化掉这条汇编！
		__asm__ volatile("hlt"); 
	}
}
```

---

## 🎯 总结：你刚才看完了什么？

> [!quote] 核心认知
> 这不是一个简单的"打印 Hello World"程序，这是计算机底层运行机制的缩影。

1. **CPU 的进化史**：从 16 位实模式，到 A20 解封，再到 `CR0` 翻转进入保护模式。
2. **内存的映射法则**：通过 Linker Script，把代码死死钉在物理内存 `0x10000` 处。
3. **硬件的直接对话**：通过 `in/out` 指令，绕过操作系统，直接配置 UART 和 VGA。
