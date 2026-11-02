#include "idt.h"
#include "io.h"

struct idt_entry idt_entries[256];
struct idt_ptr idtp;

extern void irq0_handler();
extern void irq1_handler();
extern void syscall_handler();         // user_asm.asm: int 0x80 (DPL3)
extern uint32_t exc_stubs[];           // exc.asm: 32 个异常桩的地址表

void idt_init() {
    idtp.limit = sizeof(struct idt_entry) * 256 - 1;
    idtp.base = (uint32_t)&idt_entries;

    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // 里程碑 10: 0~31 号异常全部装桩 (之前全是空门 — 用户态一崩
    // 就是 #GP→三重故障→复位循环)。硬件异常不受门 DPL 限制,
    // 用户态触发也能进 (DPL0 门只拦软件 int n)。
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, exc_stubs[i], 0x08, 0x8E);
    }

    // 设置 IRQ0 (定时器) 和 IRQ1 (键盘) 的中断门
    idt_set_gate(32, (uint32_t)irq0_handler, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1_handler, 0x08, 0x8E);

    // 里程碑 10: int 0x80 系统调用门。★DPL=3: 用户态 (CPL=3)
    // 才能用软件中断进这个门 — 这是用户程序唯一的内核大门
    idt_set_gate(0x80, (uint32_t)syscall_handler, 0x08, 0xEE);

    __asm__ volatile("lidt (%0)" : : "r"(&idtp));
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_lo = (base & 0xFFFF);
    idt_entries[num].base_hi = (base >> 16) & 0xFFFF;
    idt_entries[num].sel = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags = flags;
}
