#ifndef SERIAL_H
#define SERIAL_H
#include "types.h"
void serial_init();
void serial_putc(char c);
void serial_puts(const char* str);
void serial_put_hex(uint32_t num);  // 新增
#endif
