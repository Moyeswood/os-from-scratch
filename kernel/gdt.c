#include "gdt.h"
#include "serial.h"

// ============================================================
//  内核 GDT: 6 项 (null / 内核代码 / 内核数据 / 用户代码 / 用户数据 / TSS)
//
//  描述符编码 (Intel 手册 3.4.5):
//    [0..15] limit_lo   [16..31] base_lo
//    [32..39] base_mid  [40..47] access  [48..55] limit_hi+gran
//    [56..63] base_hi
// ============================================================

static uint64_t gdt[6];

// gdtr 结构: limit (16) + base (32), 必须 packed (lgdt 读的就是这 6 字节)
struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdtr;

tss_t tss;

extern void gdt_reload_segments();   // user_asm.asm: ljmp 刷新 CS + 重载数据段

static void gdt_set_entry(int n, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran) {
    gdt[n] = ((uint64_t)(limit & 0xFFFF) << 0)          // limit[0..15]
           | ((uint64_t)(base & 0xFFFFFF) << 16)        // base[0..23]
           | ((uint64_t)access << 40)                   // access 字节
           | ((uint64_t)((limit >> 16) & 0x0F) << 48)   // limit[16..19]
           | ((uint64_t)(gran & 0xF0) << 48)            // G/D/AVL 标志
           | ((uint64_t)((base >> 24) & 0xFF) << 56);   // base[24..31]
}

void gdt_init() {
    gdt[0] = 0;
    // 内核代码: access 0x9A (P=1 DPL0 S=1 代码/可读), gran 0xCF (G=1 D=1)
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xCF);
    // 内核数据: access 0x92 (P=1 DPL0 S=1 数据/可写), gran 0xCF
    // ★D 位必须为 1: SS 的 D/B 位决定 push/pop 默认 16 还是 32 位栈,
    //   D=0 会把内核栈当 16 位 SP 用, 一进中断就崩
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xCF);
    // 用户代码: access 0xFA (DPL=3), gran 0xCF
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xCF);
    // 用户数据: access 0xF2 (DPL=3), gran 0xCF
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xCF);
    // TSS: access 0x89 (P=1 DPL0 32 位可用 TSS), limit = 104-1 = 0x67
    gdt_set_entry(5, (uint32_t)&tss, 0x67, 0x89, 0x00);

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint32_t)&gdt;

    __asm__ volatile("lgdt (%0)" : : "r"(&gdtr));
    gdt_reload_segments();          // ljmp 0x08 刷 CS, 数据段全部重载 0x10

    // TSS: 只设 ss0/esp0 (Ring 0 栈)。esp0 在每次进入用户态前
    // 由 enter_user 设置为"当时的"内核栈顶 — 这样用户态期间来的
    // 中断/系统调用就落在那条内核调用链的栈上, 异常处理才能
    // longjmp 回 shell。
    tss.ss0 = SEL_KDATA;
    tss.esp0 = 0;
    // ★iomap_base 必须 >= TSS limit (0x68): 表示"没有 I/O 权限位图",
    // 用户态任何 in/out 都 #GP。若留 .bss 的 0, CPU 会把整个 TSS
    // 当成位图且全 0 = 所有端口对用户开放 (段/页/IO 三道边界漏一道)。
    tss.iomap_base = 0x68;
    __asm__ volatile("ltr %0" : : "r"((uint16_t)SEL_TSS));

    serial_puts("[GDT] kernel GDT: kcode/kdata/ucode/udata/tss loaded (ltr=0x28)\n\r");
}
