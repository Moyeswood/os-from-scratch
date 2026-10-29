#include "vga.h"
#include "serial.h"
#include "types.h"
#include "io.h"  // 【新增】：引入 io.h 以使用 outb

static uint16_t* vga_buffer = (uint16_t*)0xB8000;
static int cursor_x = 0;
static int cursor_y = 0;
static int vga_rows = 25;   // 屏幕行数 (字号决定: 16→25, 14→28, 12→33)

void vga_set_rows(int rows) { vga_rows = rows; }
int  vga_get_rows() { return vga_rows; }

// 【新增】：更新 VGA 硬件光标位置
static void vga_update_cursor() {
    uint16_t pos = cursor_y * 80 + cursor_x;
    outb(0x3D4, 0x0E); // 选择寄存器 14 (光标位置高 8 位)
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 0x0F); // 选择寄存器 15 (光标位置低 8 位)
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

void vga_init() {
    for (int i = 0; i < 80 * vga_rows; i++) {
        vga_buffer[i] = 0x0720;
    }
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor(); // 初始化时也更新一次硬件光标
}

void vga_clear() {
    for (int i = 0; i < 80 * vga_rows; i++) {
        vga_buffer[i] = 0x0720;
    }
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor(); // 清屏后重置光标
}

void vga_putc(char c) {
    // 【优化】：整个字符输出过程关闭中断，防止多任务切换导致显存/光标状态撕裂
    __asm__ volatile("cli");

    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        int offset = cursor_y * 80 + cursor_x;
        vga_buffer[offset] = (0x0F << 8) | c;
        cursor_x++;
        if (cursor_x >= 80) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= vga_rows) {
        // 滚屏: 整屏上移一行, 清空最后一行 (保留历史, 不再粗暴清屏 —
        // 旧版 vga_clear 会把 20+ 行输出全抹掉, help 都看不全)
        for (int y = 1; y < vga_rows; y++) {
            for (int x = 0; x < 80; x++) {
                vga_buffer[(y - 1) * 80 + x] = vga_buffer[y * 80 + x];
            }
        }
        for (int x = 0; x < 80; x++) {
            vga_buffer[(vga_rows - 1) * 80 + x] = 0x0720;
        }
        cursor_y = vga_rows - 1;
    }

    vga_update_cursor(); // 【关键】：每次字符输出后，同步硬件光标！
    __asm__ volatile("sti");
}

void vga_puts(const char* s) {
    while (*s) {
        vga_putc(*s++);
    }
}

// ---------- 编辑器用原语 ----------
// 直接写显存 (不碰光标状态, 整屏重绘用; 比逐字符 vga_putc 快得多 —
// vga_putc 每个字符都要 cli/光标同步/sti)
void vga_putc_attr_xy(int x, int y, char c, uint8_t attr) {
    if (x < 0 || x >= 80 || y < 0 || y >= vga_rows) return;
    vga_buffer[y * 80 + x] = ((uint16_t)attr << 8) | (uint8_t)c;
}

// 只移动硬件光标 (重绘完成后定位到编辑光标处)
void vga_move_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
    vga_update_cursor();
}
