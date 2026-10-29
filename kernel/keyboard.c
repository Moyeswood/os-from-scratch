#include "io.h"
#include "serial.h"
#include "vga.h"
#include "shell.h"
#include "types.h"

static int shift_pressed = 0;
static int caps_lock_on = 0;

#include "keymap.h"

void keyboard_handler() {
	uint8_t scancode = inb(0x60);
	int is_release = (scancode & 0x80);
	uint8_t key = scancode & 0x7F;

	if (key == 0x2A || key == 0x36) {
		shift_pressed = !is_release;
		return;
	}

	if (key == 0x3A && !is_release) {
		caps_lock_on = !caps_lock_on;
		return;
	}

	if (is_release) {
		return;
	}

	// 【新增】：处理方向键 (Up: 0x48, Down: 0x50)
	// 我们使用自定义控制字符 \x11 (上) 和 \x12 (下) 发送给 Shell
	if (key == 0x48) {
		shell_handle_char('\x11');
		return;
	}
	if (key == 0x50) {
		shell_handle_char('\x12');
		return;
	}

	int is_upper = shift_pressed ^ caps_lock_on;
	char c = is_upper ? keymap_shift[key] : keymap_normal[key];

	if (c != 0) {
		shell_handle_char(c);
	}
}
