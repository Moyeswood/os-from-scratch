#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init() {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static int serial_is_transmit_fifo_empty() {
    return inb(COM1 + 5) & 0x20;
}

void serial_putc(char c) {
    while (serial_is_transmit_fifo_empty() == 0);
    outb(COM1, c);
}

void serial_puts(const char* str) {
    while (*str) {
        serial_putc(*str);
        str++;
    }
}

// 重写：用纯数学计算，避免局部数组导致的栈溢出
void serial_put_hex(uint32_t num) {
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (num >> i) & 0xF;
        char c;
        if (nibble < 10) {
            c = '0' + nibble;
        } else {
            c = 'A' + nibble - 10;
        }
        serial_putc(c);
    }
}
