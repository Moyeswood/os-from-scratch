#include "io.h"
#include "serial.h"
#include "vga.h"
#include "types.h"

// 键盘状态
static int shift_pressed = 0;
static int caps_lock_on = 0;

// 普通状态映射表
static char map_normal[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Shift 状态映射表
static char map_shift[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void keyboard_handler() {
    uint8_t scancode = inb(0x60);

    // 判断是按下还是释放
    int is_release = (scancode & 0x80);
    uint8_t key = scancode & 0x7F;

    // 左 Shift (0x2A) 和 右 Shift (0x36)
    if (key == 0x2A || key == 0x36) {
        shift_pressed = !is_release;
        return;
    }

    // Caps Lock (0x3A) - 只在按下时切换
    if (key == 0x3A && !is_release) {
        caps_lock_on = !caps_lock_on;
        if (caps_lock_on) {
            vga_puts("\n[CAPS ON]\n");
        } else {
            vga_puts("\n[CAPS OFF]\n");
        }
        return;
    }

    // 忽略所有释放事件
    if (is_release) {
        return;
    }

    // 确定是否大写
    int is_upper = shift_pressed ^ caps_lock_on;

    // 查表
    char c;
    if (is_upper) {
        c = map_shift[key];
    } else {
        c = map_normal[key];
    }

    if (c != 0) {
        serial_putc(c);
        vga_putc(c);
    }
}
