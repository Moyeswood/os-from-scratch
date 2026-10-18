---
tags:
  - OSDev
  - Memory_Management
  - Assembly
  - Linker
  - Debugging
date: 2026-10-06
status:  完成
---

# 🧠 第二阶段终极指南：物理内存管理与底层原理深度剖析

> [!abstract] 本文档包含
> - 内存、地址空间、指针与汇编的底层关系
> - 汇编器“两遍扫描”与链接器“重定位表”的彻底解剖
> - 物理内存管理（位图分配器）的完整代码实现（每行代码均有详细注释）
> - 7 次真实踩坑与排错实录（BIOS 读盘、栈溢出、寄存器破坏等）

---

## 📚 第一部分：底层原理深度剖析（对话实录）

### 1.1 内存就是地址空间，这句话怎么理解？

**核心结论：**
> **内存（Memory）是物理硬件（真实的房子），而地址空间（Address Space）是 CPU 用来访问这些硬件的“门牌号系统”（地图）。**

1. **物理视角**：在 32 位 x86 系统中，CPU 的地址总线有 32 根，能发出的地址范围是 `0x00000000` 到 `0xFFFFFFFF`。这个 0 到 4GB 的范围就是“物理地址空间”。即使电脑只插了 512MB 内存，CPU 依然认为有 4GB 空间。E820 接口就是告诉 OS 这 4GB 地图中哪些是真实 RAM。
2. **硬件伪装 (MMIO)**：很多硬件设备“伪装”成了内存。例如 `0xb8000` 根本不是 RAM，而是显卡控制器在地址空间里圈出的地盘。
3. **虚拟地址空间**：现代 OS 引入 MMU 和分页机制，让每个进程以为自己独占 4GB。MMU 负责把虚拟地址翻译成物理地址。

### 1.2 C语言的指针概念在汇编语言中如何理解？

**核心结论：**
> **C 语言的指针，是逻辑上的“门牌号”；汇编语言，是 CPU 按照门牌号去“找门”的动作；而物理内存，是门后真实的“砖瓦和电路”。**

- **C 指针在汇编里的真面目**：汇编里没有“指针”类型，只有寄存器和中括号 `[]`。
  - C 代码：`*vidptr = 'A';`
  - 汇编代码：`mov byte [eax], 65` （`eax` 里存着地址 `0xb8000`）。
  - **顿悟**：C 的 `*p`（解引用）就是汇编的 `[]`；C 的 `&a`（取地址）就是把偏移量装进寄存器。

- **汇编地址如何变成物理内存**：CPU 执行指令时，通过**地址总线**输出电平信号。内存控制器收到信号后，决定是发给 RAM（翻转电容）还是发给显卡（点亮像素）。

### 1.3 在汇编里没有“变量”，只有“地址+字节”，这样理解对吗？

**完全正确！你点破了高级语言最大的“谎言”。**

在汇编和机器码的世界里，绝对没有“变量”概念。CPU 眼里只有：寄存器、地址、字节。
- C 语言的 `int count = 10;`，在汇编里只是 `mov dword [0x10000], 10`。
- `int` 和 `char` 只是编译器记住的“要读几个字节”的标记（`dword` 4字节，`byte` 1字节）。
- 汇编里的“变量名”只是标签（Label），编译后会被替换成冷冰冰的地址。

### 1.4 汇编器是怎么理解标签和真正的汇编代码的？

**核心结论：汇编器根本没有“理解”代码，它只是一个“带着计算器的无情文本替换机器”。**

它使用 **“两遍扫描（Two-Pass Assembly）”**：
1. **第一遍扫描（Pass 1）**：只算地址，不生成机器码。建立“符号表（字典）”，记录每个标签对应的偏移量。
2. **第二遍扫描（Pass 2）**：生成机器码，查字典填坑。把标签替换成相对偏移量（如跳转指令）或绝对地址（如数据访问）。

### 1.5 链接器如何修正地址？什么是重定位表？

**核心结论：如果编译器是“近视眼”，链接器就是拥有上帝视角的“总包工头”。**

- **为什么需要修正**：编译器不知道跨文件函数的最终地址，只能先挖坑（填 0），并生成一张 **“重定位表（Relocation Table）”**（欠条）。
- **重定位表的内容**：包含 `Offset`（坑的位置）、`Symbol`（要填的函数名）、`Type`（填坑工艺，如绝对地址或相对偏移）。
- **链接器的三步走**：
  1. **布局**：根据 `linker.ld` 排座位，算出绝对物理地址。
  2. **合并符号表**：把所有 `.o` 的字典合并。
  3. **地址修正**：拿着重定位表去填坑，把机器码里的 `00 00 00 00` 改成正确的偏移量。

---

## 💻 第二部分：物理内存管理代码实现（每行详细注释）

### 📂 2.1 引导扇区：`boot/boot.asm` (最终修复版)

```nasm
[BITS 16]                    ; 告诉 NASM 汇编器，以下代码是 16 位实模式代码
[ORG 0x7C00]                 ; 告诉汇编器，这段代码将被 BIOS 加载到内存的 0x7C00 处

; ================= 初始化段寄存器和栈 =================
xor ax, ax                   ; 将 AX 寄存器清零（异或自身是最高效的清零方式）
mov ds, ax                   ; 将数据段寄存器 DS 设为 0
mov es, ax                   ; 将附加段寄存器 ES 设为 0
mov ss, ax                   ; 将栈段寄存器 SS 设为 0
mov sp, 0x7C00               ; 将栈指针 SP 设为 0x7C00（栈向下生长，避开引导代码区）

; ================= 获取扩展内存大小 =================
mov ah, 0x88                 ; 设置 BIOS 中断 0x15 的子功能号 0x88（获取扩展内存大小）
int 0x15                     ; 触发 BIOS 中断，返回值（KB数）会存放在 AX 寄存器中
mov [0x500], ax              ; 将 AX 中的内存大小（KB）保存到内存地址 0x500 处，供后续 C 代码读取
; ==================================================

; 保存启动驱动器号 (DL) 到安全地址 0x600，防止被后续的 int 0x13 破坏
mov [0x600], dl              ; DL 寄存器在 BIOS 启动时包含启动盘号（通常是 0x00）

; ================= 开启 A20 地址线 =================
in al, 0x92                  ; 从系统控制端口 A (0x92) 读取一个字节到 AL
or al, 2                     ; 将 AL 的第 1 位（二进制 00000010）置为 1，开启 A20 地址线
out 0x92, al                 ; 将修改后的 AL 写回端口 0x92，解除 1MB 内存寻址限制

; ================= 循环读盘 (每次 1 个扇区，读 100 次 = 50KB) =================
mov ch, 0                    ; 设置起始柱面号 (Cylinder) 为 0
mov cl, 2                    ; 设置起始扇区号 (Sector) 为 2（扇区 1 是引导扇区）
mov dh, 0                    ; 设置起始磁头号 (Head) 为 0
mov bx, 0x1000               ; 设置目标内存的段地址为 0x1000
mov es, bx                   ; 将 ES 设为 0x1000
xor bx, bx                   ; 将 BX 清零，作为目标内存的偏移地址（物理地址 = 0x1000*16 + 0 = 0x10000）

mov si, 100                  ; 设置循环计数器 SI 为 100，表示总共要读取 100 个扇区

.read_sector:                ; 读盘循环的起始标签
    ; 【关键修复】：每次循环前必须重新设置这些寄存器，防止 BIOS 破坏！
    mov ah, 0x02             ; 设置 AH 为 0x02，表示 BIOS 0x13 中断的“读扇区”功能
    mov al, 1                ; 设置 AL 为 1，表示本次只读取 1 个扇区
    mov dl, [0x600]          ; 从内存 0x600 恢复启动驱动器号到 DL
    
    int 0x13                 ; 触发 BIOS 0x13 中断，执行读盘操作
    jc disk_error            ; 如果读盘失败（CF 标志位为 1），跳转到 disk_error 打印 'E'
    
    ; 【关键修复】：正确更新 CHS 地址，严格分离扇区号和柱面位
    mov al, cl               ; 将 CL（包含扇区号和柱面高2位）复制到 AL
    and al, 0x3F             ; 用 0x3F (00111111) 掩码提取 AL 的低 6 位，得到当前扇区号
    inc al                   ; 扇区号加 1
    cmp al, 19               ; 比较扇区号是否大于 18（标准软盘一个磁道最多 18 个扇区）
    jne .update_cl           ; 如果没超过 18，跳转到 .update_cl 直接更新 CL
    
    ; 超过 18，扇区号重置为 1，磁头号 +1
    mov al, 1                ; 新扇区号设为 1
    and cl, 0xC0             ; 用 0xC0 (11000000) 掩码保留 CL 的高 2 位（柱面高 2 位）
    or cl, al                ; 将新扇区号组合回 CL 的低 6 位
    
    inc dh                   ; 磁头号加 1
    cmp dh, 2                ; 比较磁头号是否大于等于 2（软盘有 2 个磁头）
    jne .next                ; 如果没超过 2，跳转到 .next
    mov dh, 0                ; 磁头号重置为 0
    inc ch                   ; 柱面号加 1
    jmp .next                ; 跳转到 .next

.update_cl:                  ; 更新 CL 标签
    and cl, 0xC0             ; 保留 CL 的高 2 位（柱面高 2 位）
    or cl, al                ; 将计算好的新扇区号组合回 CL 的低 6 位

.next:                       ; 下一个扇区标签
    add bx, 512              ; 内存偏移地址 BX 增加 512 字节（一个扇区的大小）
    
    dec si                   ; 循环计数器 SI 减 1
    jnz .read_sector         ; 如果 SI 不为 0（还没读完 100 个扇区），跳回 .read_sector 继续循环
; ==============================================================================

cli                          ; 关闭中断（CLI），在切换 CPU 模式时绝不能被打断
lgdt [gdt_desc]              ; 将 gdt_desc 指向的内存数据加载到 CPU 的 GDTR 寄存器中
mov eax, cr0                 ; 将控制寄存器 CR0 的值读到 EAX
or eax, 1                    ; 将 EAX 的最低位（PE 位，Protection Enable）置为 1
mov cr0, eax                 ; 将修改后的 EAX 写回 CR0，CPU 硬件电路翻转，进入 32 位保护模式

jmp dword 0x08:pmode         ; 远跳转。0x08 是代码段选择子，强制 CPU 切换到 32 位指令解码

disk_error:                  ; 读盘错误处理标签
    mov ah, 0x0E             ; 设置 BIOS 0x10 中断的子功能号 0x0E（打印字符）
    mov al, 'E'              ; 将字符 'E' (Error) 放入 AL
    int 0x10                 ; 触发 BIOS 0x10 中断，在屏幕上打印 'E'
    jmp $                    ; 死循环（$ 代表当前地址），防止 CPU 执行乱码

[BITS 32]                    ; 告诉 NASM，以下代码是 32 位保护模式代码
pmode:                       ; 保护模式入口标签
    mov ax, 0x10             ; 0x10 是数据段选择子
    mov ds, ax               ; 初始化数据段寄存器 DS
    mov es, ax               ; 初始化附加段寄存器 ES
    mov fs, ax               ; 初始化 FS 段寄存器
    mov gs, ax               ; 初始化 GS 段寄存器
    mov ss, ax               ; 初始化栈段寄存器 SS
    mov esp, 0x90000         ; 设置 32 位下的栈顶指针 ESP 为 0x90000

    jmp 0x10000              ; 跳转到内存的 0x10000 处，即 C 语言内核的入口！

; ================= GDT (全局描述符表) 定义 =================
gdt_start:                   ; GDT 起始标签
    dq 0                     ; 第 0 个描述符必须是 0（Intel 硬性规定）
gdt_code:                    ; 代码段描述符标签
    dw 0xFFFF                ; 段界限 0-15 位
    dw 0x0000                ; 基地址 0-15 位
    db 0x00                  ; 基地址 16-23 位
    db 10011010b             ; 访问权限：Present=1, DPL=0(Ring 0), 代码段, 可执行/可读
    db 11001111b             ; 标志：Granularity=1(4KB粒度), D/B=1(32位), 界限高4位=F
    db 0x00                  ; 基地址 24-31 位
gdt_data:                    ; 数据段描述符标签
    dw 0xFFFF                ; 段界限 0-15 位
    dw 0x0000                ; 基地址 0-15 位
    db 0x00                  ; 基地址 16-23 位
    db 10010010b             ; 访问权限：数据段, 可读写
    db 11001111b             ; 标志：Granularity=1, D/B=1, 界限高4位=F
    db 0x00                  ; 基地址 24-31 位
gdt_end:                     ; GDT 结束标签

gdt_desc:                    ; GDT 指针描述符标签
    dw gdt_end - gdt_start - 1 ; GDT 表的总长度（字节数减 1）
    dd gdt_start             ; GDT 表在内存中的物理起始地址

times 510 - ($ - $$) db 0    ; 用 0 填充，直到当前偏移量达到 510 字节（$是当前地址，$$是段起始地址）
dw 0xAA55                    ; 引导扇区的魔数。BIOS 靠它判断是否为合法引导程序
```

---

### 📂 2.2 内存管理头文件：`kernel/memory.h`

```c
#ifndef MEMORY_H             // 防止头文件被重复包含的宏定义开始
#define MEMORY_H             // 定义宏 MEMORY_H

#include "types.h"           // 引入基础类型定义（如 uint8_t, uint32_t）

// 内存管理初始化函数声明
void memory_init();

// 分配一个物理页（4KB），返回分配到的物理地址
uint32_t alloc_page();

// 释放一个物理页，参数 addr 是要释放的物理页的起始地址
void free_page(uint32_t addr);

#endif                       // 防止头文件被重复包含的宏定义结束
```

---

###  2.3 位图分配器实现：`kernel/memory.c`

```c
#include "memory.h"          // 引入内存管理头文件
#include "serial.h"          // 引入串口驱动头文件（用于打印调试日志）

#define PAGE_SIZE 4096       // 定义每页大小为 4096 字节（4KB）
#define BITMAP_BASE 0x100000 // 定义位图在内存中的起始物理地址（1MB 处）

static uint8_t* bitmap;      // 定义位图指针，指向位图在内存中的位置
static uint32_t bitmap_size; // 定义位图占用的字节数
static uint32_t total_pages; // 定义系统总的物理页数

void memory_init() {
    serial_puts("\n\r[Memory] Initializing memory manager...\n\r"); // 打印初始化开始日志
    
    // 从 0x500 读取 boot.asm 保存的扩展内存大小 (KB)
    uint16_t ext_mem_kb = *(uint16_t*)0x500; // 将内存地址 0x500 处的 2 字节数据读取为 uint16_t
    serial_puts("[Memory] Extended memory: "); // 打印扩展内存提示
    serial_put_hex(ext_mem_kb);              // 以十六进制格式打印扩展内存大小
    serial_puts(" KB\n\r");                // 打印单位 KB 和换行
    
    // 总内存 = 1MB (基础内存) + 扩展内存
    uint32_t total_mem = 1024 * 1024 + ext_mem_kb * 1024; // 计算总内存字节数
    total_pages = total_mem / PAGE_SIZE;     // 计算总页数（总字节数除以 4KB）
    bitmap_size = (total_pages + 7) / 8;     // 计算位图字节数（每页用 1 bit 表示，8 bit = 1 byte，向上取整）
    
    serial_puts("[Memory] Total pages: ");   // 打印总页数提示
    serial_put_hex(total_pages);             // 以十六进制格式打印总页数
    serial_puts("\n\r");                   // 打印换行
    
    // 位图放在 1MB 处
    bitmap = (uint8_t*)BITMAP_BASE;          // 将位图指针指向物理地址 0x100000
    
    // 初始化位图：全部标记为已分配 (1)
    for (uint32_t i = 0; i < bitmap_size; i++) { // 遍历位图的每一个字节
        bitmap[i] = 0xFF;                    // 将每个字节设为 0xFF（二进制 11111111，表示 8 页都被占用）
    }
    
    // 标记可用内存为空闲 (0)
    // 跳过 0-1MB (内核和位图占用)
    uint32_t usable_start = BITMAP_BASE + bitmap_size; // 计算可用内存的起始地址（位图之后）
    // 对齐到页边界（确保起始地址是 4KB 的整数倍）
    usable_start = (usable_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); 
    
    uint32_t start_page = usable_start / PAGE_SIZE;    // 计算可用内存的起始页号
    for (uint32_t p = start_page; p < total_pages; p++) { // 从起始页遍历到最后一页
        uint32_t byte_idx = p / 8;           // 计算该页在位图中对应的字节索引
        uint8_t bit_idx = p % 8;             // 计算该页在对应字节中的位索引（0-7）
        if (byte_idx < bitmap_size) {        // 确保字节索引没有越界
            bitmap[byte_idx] &= ~(1 << bit_idx); // 将对应的位清零（0 表示空闲），~(1 << bit_idx) 生成掩码
        }
    }
    
    serial_puts("[Memory] Memory manager ready!\n\r"); // 打印内存管理器就绪日志
}

uint32_t alloc_page() {
    for (uint32_t i = 0; i < bitmap_size; i++) { // 遍历位图的每一个字节
        if (bitmap[i] != 0xFF) {             // 如果该字节不是全 1，说明里面有空闲页
            for (int bit = 0; bit < 8; bit++) { // 遍历该字节的 8 个位
                if (!(bitmap[i] & (1 << bit))) { // 检查第 bit 位是否为 0（空闲）
                    bitmap[i] |= (1 << bit); // 将该位置为 1（标记为已分配）
                    return (i * 8 + bit) * PAGE_SIZE; // 计算并返回该页的物理地址
                }
            }
        }
    }
    return 0;                                // 如果没有找到空闲页，返回 0 表示分配失败
}

void free_page(uint32_t addr) {
    uint32_t page = addr / PAGE_SIZE;        // 根据物理地址计算页号
    uint32_t byte_idx = page / 8;            // 计算该页在位图中对应的字节索引
    uint8_t bit_idx = page % 8;              // 计算该页在对应字节中的位索引
    if (byte_idx < bitmap_size) {            // 确保字节索引没有越界
        bitmap[byte_idx] &= ~(1 << bit_idx); // 将对应的位清零（标记为空闲）
    }
}
```

---

### 📂 2.4 串口驱动更新：`kernel/serial.c` (修复栈溢出版)

```c
#include "serial.h"          // 引入串口驱动头文件
#include "io.h"              // 引入底层 I/O 端口操作头文件（inb, outb）

#define COM1 0x3F8           // 定义 COM1 串口的基地址为 0x3F8

void serial_init() {
    outb(COM1 + 1, 0x00);    // 向端口 0x3F9 写 0，禁用串口中断（使用轮询模式）
    outb(COM1 + 3, 0x80);    // 向端口 0x3FB 写 0x80，启用 DLAB（除数锁存访问位），准备设置波特率
    outb(COM1 + 0, 0x03);    // 向端口 0x3F8 写 0x03，设置波特率除数低字节（115200 / 38400 = 3）
    outb(COM1 + 1, 0x00);    // 向端口 0x3F9 写 0x00，设置波特率除数高字节
    outb(COM1 + 3, 0x03);    // 向端口 0x3FB 写 0x03，禁用 DLAB，设置 8 位数据位，无校验，1 停止位
    outb(COM1 + 2, 0xC7);    // 向端口 0x3FA 写 0xC7，启用 FIFO，清空收发缓冲，14 字节触发阈值
    outb(COM1 + 4, 0x0B);    // 向端口 0x3FC 写 0x0B，启用 IRQ，设置 RTS/DSR 引脚状态
}

static int serial_is_transmit_fifo_empty() {
    return inb(COM1 + 5) & 0x20; // 读取线路状态寄存器 (0x3FD)，检查第 5 位（发送保持寄存器空）是否为 1
}

void serial_putc(char c) {
    while (serial_is_transmit_fifo_empty() == 0); // 死循环等待，直到发送缓冲区为空
    outb(COM1, c);           // 将字符 c 写入数据寄存器 (0x3F8)，硬件会自动发送
}

void serial_puts(const char* str) {
    while (*str) {           // 遍历字符串，直到遇到结束符 '\0'
        serial_putc(*str);   // 逐个字符调用 serial_putc 发送
        str++;               // 指针后移，指向下一个字符
    }
}

// 【关键修复】：用纯数学计算，避免局部数组导致的栈溢出
void serial_put_hex(uint32_t num) {
    serial_puts("0x");       // 打印十六进制前缀 "0x"
    for (int i = 28; i >= 0; i -= 4) { // 从最高 4 位（第 28 位）开始，每次右移 4 位，直到第 0 位
        uint8_t nibble = (num >> i) & 0xF; // 提取当前的 4 位（nibble），范围 0-15
        char c;              // 定义字符变量 c
        if (nibble < 10) {   // 如果 nibble 小于 10（0-9）
            c = '0' + nibble; // 转换为 ASCII 字符 '0'-'9'
        } else {             // 如果 nibble 大于等于 10（10-15）
            c = 'A' + nibble - 10; // 转换为 ASCII 字符 'A'-'F'
        }
        serial_putc(c);      // 发送转换后的字符
    }
}
```

---

### 📂 2.5 内核主函数：`kernel/kernel.c`

```c
#include "serial.h"          // 引入串口驱动
#include "vga.h"             // 引入 VGA 显示驱动
#include "idt.h"             // 引入中断描述符表
#include "memory.h"          // 引入内存管理
#include "types.h"           // 引入基础类型

extern void pic_remap();     // 外部声明 PIC 重映射函数（在 pic.c 中定义）

void kernel_main() {
    serial_init();           // 初始化串口
    serial_puts("\n\r[1] Serial OK\n\r"); // 打印串口初始化成功日志

    vga_init();              // 初始化 VGA 屏幕（清屏）
    vga_puts("OS Booting...\n"); // 在屏幕上打印启动信息
    serial_puts("[2] VGA OK\n\r"); // 打印 VGA 初始化成功日志

    serial_puts("[3] Before memory_init\n\r"); // 打印内存初始化前日志
    memory_init();           // 初始化内存管理器（解析 E820，建立位图）
    serial_puts("[4] memory_init DONE\n\r"); // 打印内存初始化完成日志
    
    serial_puts("[5] Before alloc_page test\n\r"); // 打印内存测试前日志
    uint32_t page1 = alloc_page(); // 分配第一个物理页
    serial_puts("[6] alloc_page 1: "); // 打印分配结果提示
    serial_put_hex(page1);   // 以十六进制打印 page1 的地址
    serial_puts("\n\r");   // 打印换行
    
    uint32_t page2 = alloc_page(); // 分配第二个物理页
    serial_puts("[7] alloc_page 2: "); // 打印分配结果提示
    serial_put_hex(page2);   // 以十六进制打印 page2 的地址
    serial_puts("\n\r");   // 打印换行
    
    free_page(page2);        // 释放第二个物理页（测试内存回收）
    uint32_t page3 = alloc_page(); // 再次分配物理页（应该复用 page2 的地址）
    serial_puts("[8] alloc_page 3 (reuse): "); // 打印复用结果提示
    serial_put_hex(page3);   // 以十六进制打印 page3 的地址
    serial_puts("\n\r");   // 打印换行
    
    if (page3 == page2) {    // 检查 page3 是否等于 page2
        serial_puts("[9] Memory test SUCCESS\n\r"); // 相等则打印测试成功
    } else {
        serial_puts("[9] Memory test FAIL\n\r"); // 不相等则打印测试失败
    }

    serial_puts("[10] Before idt_init\n\r"); // 打印中断初始化前日志
    idt_init();              // 初始化中断描述符表（IDT）
    serial_puts("[11] idt_init DONE\n\r"); // 打印 IDT 初始化完成日志
    
    serial_puts("[12] Before pic_remap\n\r"); // 打印 PIC 重映射前日志
    pic_remap();             // 重新映射 8259A PIC（避免与 CPU 异常冲突）
    serial_puts("[13] pic_remap DONE\n\r"); // 打印 PIC 重映射完成日志

    serial_puts("[14] Before STI\n\r"); // 打印开启中断前日志
    __asm__ volatile("sti"); // 执行 STI 指令，开启 CPU 中断响应
    serial_puts("[15] STI DONE (Interrupts Enabled)\n\r"); // 打印中断开启成功日志

    serial_puts("\n\r[Kernel] System fully ready!\n\r"); // 打印系统完全就绪日志
    vga_puts("Memory & Interrupts OK!\n"); // 在屏幕上打印最终状态
    vga_puts("Press keys...\n"); // 提示用户可以按键

    while(1) {               // 进入内核空闲死循环
        __asm__ volatile("hlt"); // 执行 HLT 指令，让 CPU 休眠，等待中断唤醒
    }
}
```

---

## 🐛 第三部分：踩坑与排错实录（一字不删）

### 错误 1：链接错误 `undefined reference to '__stack_chk_fail'`
**现象**：
```text
serial.c:(.text+0x16f): undefined reference to `__stack_chk_fail'
```
**原因**：现代 GCC 默认开启栈溢出保护（SSP），会在函数中插入检查代码。但在裸机环境下没有 C 标准库，找不到 `__stack_chk_fail` 的实现。
**解决**：在 Makefile 的 C 编译参数中添加 `-fno-stack-protector` 禁用栈保护。

### 错误 2：屏幕闪烁（无限重启）- 第一次
**现象**：系统启动后屏幕不断闪烁，串口日志只打印了前几行。
**原因**：内核代码变大（加入了 memory.c 等），但 `boot.asm` 里只读取了 20 个扇区（10KB），导致部分内核代码没有被加载到内存中。CPU 执行到全是 0 的内存区域，触发 Triple Fault 重启。
**解决**：将读盘扇区数从 20 增加到 60（30KB），并加入 15 步精准串口日志。

### 错误 3：屏幕闪烁（无限重启）- 第二次
**现象**：日志打印到 `[Memory] Extended memory:` 后崩溃。
**原因**：`serial_put_hex` 函数内部使用了局部数组 `const char hex_chars[] = "0123456789ABCDEF";`。在裸机环境下，这会导致栈空间耗尽或访问越界，引发崩溃。
**解决**：重写 `serial_put_hex`，使用纯数学计算（`if (nibble < 10) c = '0' + nibble;`）代替局部数组，彻底消除栈溢出风险。并将读盘扇区增加到 200。

### 错误 4：屏幕显示字母 'E' - 第一次
**现象**：QEMU 屏幕只显示一个 'E'，说明 BIOS 读盘失败。
**原因**：请求 BIOS 一次性读取 200 个扇区。但 BIOS 的 `int 0x13` 不支持跨越磁道边界读取（标准软盘一个磁道只有 18 个扇区）。读到第 18 个扇区时报错。
**解决**：改为循环读盘，每次只读取 1 个扇区，循环 100 次（50KB）。

### 错误 5：编译错误 `invalid combination of opcode and operands`
**现象**：
```text
boot/boot.asm:53: error: invalid combination of opcode and operands
```
**原因**：在循环读盘代码中使用了 `inc es`。在 x86 汇编中，`inc` 指令不能直接用于段寄存器（如 `es`）。而且 50KB 的内存偏移根本不会导致 `bx` 溢出，无需处理进位。
**解决**：去掉错误的 `inc es` 和进位处理代码，直接 `add bx, 512`。

### 错误 6：屏幕显示字母 'E' - 第二次
**现象**：修改后依然显示 'E'。
**原因**：`int 0x13` 中断执行后，会**破坏 `AH` 和 `AL` 寄存器**（`AH` 变成状态码，`AL` 变成实际读取数）。第二次循环时，`AH` 已经不是 `0x02`（读扇区功能号）了，导致 BIOS 执行了错误的功能。
**解决**：在每次调用 `int 0x13` 之前，重新设置 `mov ah, 0x02`、`mov al, 1` 和 `mov dl, [0x600]`。

### 错误 7：屏幕显示字母 'E' - 第三次（最终修复）
**现象**：依然显示 'E'。
**原因**：CHS 寻址逻辑错误。`CL` 寄存器不仅包含扇区号（低 6 位），还包含柱面的高 2 位（第 6、7 位）。当扇区号超过 18 时，代码执行了 `mov cl, 1`，这不仅重置了扇区号，还**把柱面的高 2 位清零了**，导致 BIOS 读取了错误的扇区。
**解决**：严格分离扇区号和柱面位。使用 `and cl, 0xC0` 保留高 2 位，使用 `and al, 0x3F` 提取低 6 位进行计算，最后用 `or` 组合回去。

---

## 🎯 第四部分：最终成功与总结

### 成功日志
```text
[1] Serial OK
[2] VGA OK
[3] Before memory_init

[Memory] Initializing memory manager...
[Memory] Extended memory: 0x0000FC00 KB
[Memory] Total pages: 0x00004000
[Memory] Memory manager ready!
[4] memory_init DONE
[5] Before alloc_page test
[6] alloc_page 1: 0x00101000
[7] alloc_page 2: 0x00102000
[8] alloc_page 3 (reuse): 0x00102000
[9] Memory test SUCCESS
[10] Before idt_init
[11] idt_init DONE
[12] Before pic_remap
[13] pic_remap DONE
[14] Before STI
[15] STI DONE (Interrupts Enabled)

[Kernel] System fully ready!
```
*(注：用户随后按下了 Shift 键，终端输出了 `kkkk MMM )( <> "" +=**% $3 $4#3 !!~``，完美验证了键盘驱动的状态机逻辑。)*

### 核心知识点总结
1. **位图分配器**：将物理内存按 4KB 切页，用 1 个 bit 标记一页的占用状态。`alloc_page` 找 0 置 1，`free_page` 置 0。
2. **BIOS 读盘陷阱**：`int 0x13` 会破坏寄存器，必须每次循环前重置；CHS 寻址必须严格分离 CL 寄存器的扇区位和柱面位。
3. **裸机栈保护**：必须禁用 GCC 的 SSP（`-fno-stack-protector`），且避免在函数内使用大型局部数组。

---

**恭喜你完成了操作系统开发中最难、最核心的部分之一！你的内核现在不仅能感知世界（中断），还能掌控资源（内存）了！** 
