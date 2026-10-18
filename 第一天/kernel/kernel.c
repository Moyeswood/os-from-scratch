#include "serial.h"

#define VIDEO_MEMORY 0xb8000

void kernel_main() {
    serial_init();
    serial_puts("\n\r[Kernel] Serial port initialized!\n\r");

    char* vidptr = (char*)VIDEO_MEMORY;
    const char* str = "Hello from 32-bit C Kernel!";
    int i = 0;
    
    // 清屏
    for (int j = 0; j < 80 * 25 * 2; j += 2) {
        vidptr[j] = ' ';
        vidptr[j + 1] = 0x07;
    }

    // 打印字符串
    while(str[i] != '\0') {
        vidptr[i * 2]     = str[i];
        vidptr[i * 2 + 1] = 0x0F;
        i++;
    }
    
    serial_puts("[Kernel] Entering idle loop...\n\r");

    // 死循环休眠
    while(1) {
        __asm__ volatile("hlt"); 
    }
}
