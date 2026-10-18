#include "io.h"
#include "types.h"

#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1

void pic_remap() {
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    // 【修复】屏蔽 IRQ0，只开放 IRQ1 (键盘)
    outb(PIC1_DATA, 0xFD);  // 11111101 - 只开放 IRQ1
    outb(PIC2_DATA, 0xFF);
}
