#ifndef GDT_H
#define GDT_H

#include "types.h"

// ============================================================
//  内核自己的 GDT (里程碑 10: Ring 3 用户态)
//
//  boot.asm 的 GDT 只有 3 项 (null/代码/数据, 全 DPL0),
//  跑不了用户态代码。内核启动后用这张 6 项的表替换它:
//
//    0x08 内核代码段  DPL0
//    0x10 内核数据段  DPL0
//    0x1B 用户代码段  DPL3   (0x18 | RPL3)
//    0x23 用户数据段  DPL3   (0x20 | RPL3)
//    0x28 TSS          DPL0   (中断/系统调用时 CPU 从这里取内核栈)
//
//  段基址全部 0 / 上限 4GB — 仍然是"平坦"模型,
//  内核与用户的隔离不靠段, 靠页表的 U/S 位 (见 paging.c)。
// ============================================================

#define SEL_KCODE 0x08
#define SEL_KDATA 0x10
#define SEL_UCODE 0x1B
#define SEL_UDATA 0x23
#define SEL_TSS   0x28

// 32 位 TSS (Intel 手册图 7-2), 布局必须一字不差 (共 104 字节)。
// 我们只用 ss0/esp0: 从 Ring 3 进中断/系统调用时,
// CPU 会先把栈切到 esp0 指向的内核栈, 再压返回帧。
typedef struct {
    uint32_t link;         // 0x00 前一任务链接 (未用)
    uint32_t esp0;         // 0x04 ★Ring 0 栈指针 (enter_user 每次进入用户态前设置)
    uint16_t ss0;          // 0x08 ★Ring 0 栈段 (0x10)
    uint16_t _r1;          // 0x0A
    uint32_t esp1;         // 0x0C
    uint16_t ss1;          // 0x10
    uint16_t _r2;          // 0x12
    uint32_t esp2;         // 0x14
    uint16_t ss2;          // 0x18
    uint16_t _r3;          // 0x1A
    uint32_t cr3;          // 0x1C
    uint32_t eip;          // 0x20
    uint32_t eflags;       // 0x24
    uint32_t eax;          // 0x28
    uint32_t ecx;          // 0x2C
    uint32_t edx;          // 0x30
    uint32_t ebx;          // 0x34
    uint32_t esp;          // 0x38
    uint32_t ebp;          // 0x3C
    uint32_t esi;          // 0x40
    uint32_t edi;          // 0x44
    uint16_t es;           // 0x48
    uint16_t _r4;          // 0x4A
    uint16_t cs;           // 0x4C
    uint16_t _r5;          // 0x4E
    uint16_t ss;           // 0x50
    uint16_t _r6;          // 0x52
    uint16_t ds;           // 0x54
    uint16_t _r7;          // 0x56
    uint16_t fs;           // 0x58
    uint16_t _r8;          // 0x5A
    uint16_t gs;           // 0x5C
    uint16_t _r9;          // 0x5E
    uint16_t ldt;          // 0x60
    uint16_t _r10;         // 0x62
    uint16_t _r11;         // 0x64
    uint16_t iomap_base;   // 0x66 无 I/O 权限位图 (不设 = 用户态任何端口都碰不得)
} __attribute__((packed)) tss_t;

// 编译期断言: TSS 大小必须是 104 字节 (错一位 CPU 就会乱取 esp0)
typedef char tss_size_check[(sizeof(tss_t) == 104) ? 1 : -1];

// 全局 TSS (gdt.c 定义; user_asm.asm 直接写 tss.esp0, 所以不能 static)
extern tss_t tss;

void gdt_init();

#endif
