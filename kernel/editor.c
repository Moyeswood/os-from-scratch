// ============================================================
//  内置文本编辑器 (nano 风格教学版)
//
//  架构 (与游戏同款): edit 命令在 shell 里执行 → 整个编辑器
//  跑在 IRQ1 键盘处理器的上下文里 → 后续按键不会产生新 IRQ1
//  (同优先级在服务中), 所以编辑器自己轮询 8042 读原始扫描码,
//  自己跟踪 Shift/Ctrl/Caps 状态和 0xE0 扩展键前缀。
//
//  数据模型: 单一字符缓冲 ed_buf[48KB] (与文件系统单文件上限
//  一致), 行以 '\n' 分隔, 光标 = 字节偏移。行号/列号按需
//  线性扫描计算 (48KB 在 TCG 下微秒级, 教学版不优化)。
//
//  屏幕布局 (纯文本模式, 不需要模式切换):
//    0 .. rows-3: 文本视口 (80 列, 长行水平滚动, 光标格反显)
//    rows-2:      状态栏 (反显): 快捷键 | L行 C列 | 文件名 [M]
//    rows-1:      消息行 (反显): 保存结果/警告等
//
//  键位: 方向键/Home/End/PgUp/PgDn 导航, Enter 换行,
//  Backspace/Delete 删除, Tab = 4 空格, Ctrl+S 保存, Ctrl+X 退出。
//
//  串口标记 (测试预言机): [ED] new/open/saved/modified warning/exit/fail
// ============================================================

#include "editor.h"
#include "fs.h"
#include "vga.h"
#include "keymap.h"
#include "string.h"
#include "serial.h"
#include "shell.h"
#include "io.h"
#include "types.h"

#define ED_MAX     49152   // 缓冲上限 48KB (= 文件系统单文件上限)
#define ED_TAB     4       // Tab 展开宽度
#define ED_COLS    80      // 所有字号都是 80 列

static char ed_buf[ED_MAX];
static uint32_t ed_len;          // 缓冲长度 (字节)
static uint32_t ed_cur;          // 光标 (字节偏移)
static int ed_modified;
static int ed_warn_quit;         // 有修改时第一次 ^X → 警告
static char ed_msg[64];          // 消息行文本
static uint32_t ed_inum;         // 文件 inode
static char ed_path[FS_PATH_MAX];
static int ed_top_line;          // 视口第一行 (垂直滚动)
static int ed_left_col;          // 水平滚动列
static int ed_rows;              // 屏幕行数 (字号决定)
static int ed_view_rows;         // 文本视口行数 = rows - 2

// ---------- 位置计算 ----------
// 偏移 off 的行号/列号 (从 0 起)
static void ed_pos(uint32_t off, int* line, int* col) {
    int l = 0, c = 0;
    for (uint32_t i = 0; i < off && i < ed_len; i++) {
        if (ed_buf[i] == '\n') { l++; c = 0; }
        else c++;
    }
    *line = l; *col = c;
}

// 第 line 行的起始偏移
static uint32_t ed_line_start(int line) {
    uint32_t off = 0;
    int l = 0;
    while (l < line && off < ed_len) {
        if (ed_buf[off] == '\n') l++;
        off++;
    }
    return off;
}

// 光标所在行的起始偏移
static uint32_t ed_cur_line_start(void) {
    uint32_t off = ed_cur;
    while (off > 0 && ed_buf[off - 1] != '\n') off--;
    return off;
}

// ---------- 编辑操作 ----------
static int ed_insert(uint32_t off, char c) {
    if (ed_len >= ED_MAX) return -1;
    for (uint32_t i = ed_len; i > off; i--) ed_buf[i] = ed_buf[i - 1];
    ed_buf[off] = c;
    ed_len++;
    ed_cur = off + 1;
    ed_modified = 1;
    ed_warn_quit = 0;
    return 0;
}

// 删除 off 处字符 (光标不动)
static void ed_delete_at(uint32_t off) {
    if (ed_len == 0 || off >= ed_len) return;
    for (uint32_t i = off; i < ed_len - 1; i++) ed_buf[i] = ed_buf[i + 1];
    ed_len--;
    ed_modified = 1;
    ed_warn_quit = 0;
}

// ---------- 光标移动 ----------
static void ed_left(void)  { if (ed_cur > 0) ed_cur--; }
static void ed_right(void) { if (ed_cur < ed_len) ed_cur++; }
static void ed_home(void)  { ed_cur = ed_cur_line_start(); }
static void ed_end(void) {
    while (ed_cur < ed_len && ed_buf[ed_cur] != '\n') ed_cur++;
}

static void ed_up(void) {
    int line, col;
    ed_pos(ed_cur, &line, &col);
    if (line == 0) return;
    uint32_t ls = ed_line_start(line - 1);
    uint32_t le = ed_line_start(line);      // 上一行的 [ls, le)
    uint32_t target = ls + (uint32_t)col;
    if (target >= le && le > ls) target = le - 1;
    ed_cur = target;
}

static void ed_down(void) {
    int line, col;
    ed_pos(ed_cur, &line, &col);
    uint32_t ls = ed_line_start(line + 1);
    if (ls > ed_len) return;                // 已在最后一行
    uint32_t le = ed_line_start(line + 2);
    uint32_t target = ls + (uint32_t)col;
    if (target >= le && le > ls) target = le - 1;
    if (target > ed_len) target = ed_len;
    ed_cur = target;
}

// ---------- 渲染 ----------
static void ed_scroll_into_view(void) {
    int line, col;
    ed_pos(ed_cur, &line, &col);
    if (line < ed_top_line) ed_top_line = line;
    if (line >= ed_top_line + ed_view_rows) ed_top_line = line - ed_view_rows + 1;
    if (col < ed_left_col) ed_left_col = col;
    if (col >= ed_left_col + ED_COLS) ed_left_col = col - ED_COLS + 1;
}

// 数字 → 字符串 (无 snprintf)
static void ed_uint_to_str(uint32_t n, char* out) {
    if (n == 0) { out[0] = '0'; out[1] = '\0'; return; }
    char tmp[12];
    int p = 0;
    while (n) { tmp[p++] = (char)('0' + n % 10); n /= 10; }
    for (int i = 0; i < p; i++) out[i] = tmp[p - 1 - i];
    out[p] = '\0';
}

static void ed_render(void) {
    ed_scroll_into_view();
    int line, col;
    ed_pos(ed_cur, &line, &col);

    // 1. 文本视口 (不折行, 长行水平滚动)
    uint32_t line_off = ed_line_start(ed_top_line);
    for (int y = 0; y < ed_view_rows; y++) {
        uint32_t end = line_off;
        while (end < ed_len && ed_buf[end] != '\n') end++;
        for (int x = 0; x < ED_COLS; x++) {
            uint32_t src = line_off + (uint32_t)(ed_left_col + x);
            char c = (src < end) ? ed_buf[src] : ' ';
            vga_putc_attr_xy(x, y, c, (src == ed_cur) ? 0x70 : 0x0F);
        }
        line_off = end + 1;                  // 下一行起点 (end==ed_len 时为文件尾+1)
    }

    // 2. 状态栏 (反显): ^S Save  ^X Exit | L行 C列 | 路径 [M]
    static char status[ED_COLS + 1];
    k_memset(status, ' ', ED_COLS);
    int pos = 0;
    const char* s = " ^S Save  ^X Exit | L";
    while (*s) status[pos++] = *s++;
    char num[12];
    ed_uint_to_str((uint32_t)line + 1, num);
    s = num; while (*s) status[pos++] = *s++;
    status[pos++] = ' ';
    status[pos++] = 'C';
    ed_uint_to_str((uint32_t)col + 1, num);
    s = num; while (*s) status[pos++] = *s++;
    status[pos++] = ' ';
    status[pos++] = '|';
    status[pos++] = ' ';
    s = ed_path;
    while (*s && pos < ED_COLS - 4) status[pos++] = *s++;
    if (ed_modified) {
        status[pos++] = ' ';
        status[pos++] = '[';
        status[pos++] = 'M';
        status[pos++] = ']';
    }
    for (int x = 0; x < ED_COLS; x++) vga_putc_attr_xy(x, ed_rows - 2, status[x], 0x70);

    // 3. 消息行 (反显)
    for (int x = 0; x < ED_COLS; x++) {
        char c = (x < (int)k_strlen(ed_msg)) ? ed_msg[x] : ' ';
        vga_putc_attr_xy(x, ed_rows - 1, c, 0x70);
    }

    // 4. 硬件光标 → 编辑光标处
    vga_move_cursor(col - ed_left_col, line - ed_top_line);
}

// ---------- 保存 ----------
static void ed_save(void) {
    int r = fs_write(ed_inum, ed_buf, ed_len, 0);   // 截断写
    if (r < 0) {
        k_strcpy(ed_msg, "Save failed!");
        serial_puts("[ED] save fail\n\r");
        return;
    }
    ed_modified = 0;
    k_strcpy(ed_msg, "Saved ");
    ed_uint_to_str((uint32_t)r, ed_msg + k_strlen(ed_msg));
    k_strcpy(ed_msg + k_strlen(ed_msg), " bytes");
    serial_puts("[ED] saved: "); serial_puts(ed_path);
    serial_puts(" ("); serial_put_hex((uint32_t)r); serial_puts(" bytes)\n\r");
}

// ---------- 按键轮询 (游戏同款) ----------
static int ed_poll(void) {
    if (!(inb(0x64) & 1)) return -1;
    return inb(0x60);
}

// ---------- 主入口 ----------
void editor_run(const char* path) {
    k_strcpy(ed_path, path);
    ed_len = 0; ed_cur = 0; ed_modified = 0; ed_warn_quit = 0;
    ed_msg[0] = '\0';
    ed_top_line = 0; ed_left_col = 0;
    ed_rows = vga_get_rows();
    ed_view_rows = ed_rows - 2;

    // 打开 (或新建)
    ed_inum = (uint32_t)fs_lookup(path);
    if (ed_inum == (uint32_t)-1) {
        ed_inum = (uint32_t)fs_create(path, FS_FILE);
        if (ed_inum == (uint32_t)-1) {
            serial_puts("[ED] fail: cannot create\n\r");
            vga_puts("\n\redit: cannot create file (parent missing? name too long?)\n\r");
            return;
        }
        k_strcpy(ed_msg, "New file");
        serial_puts("[ED] new: "); serial_puts(path); serial_puts("\n\r");
    } else {
        char probe[1];
        if (fs_read(ed_inum, probe, 0, 1) < 0) {
            serial_puts("[ED] fail: is a directory\n\r");
            vga_puts("\n\redit: Is a directory\n\r");
            return;
        }
        uint32_t off = 0;
        int r;
        while ((r = fs_read(ed_inum, ed_buf + off, off, 4096)) > 0) {
            off += (uint32_t)r;
            if (off >= ED_MAX) break;
        }
        ed_len = off;
        serial_puts("[ED] open: "); serial_puts(path);
        serial_puts(" ("); serial_put_hex(off); serial_puts(" bytes)\n\r");
    }

    // 排空触发本命令的回车键释放码 (游戏同款陷阱)
    kbd_drain();

    int shift = 0, ctrl = 0, caps = 0, e0 = 0;
    ed_render();
    while (1) {
        int sc = ed_poll();
        if (sc < 0) { __asm__ volatile("hlt"); continue; }

        if (sc == 0xE0) { e0 = 1; continue; }       // 扩展键前缀
        int release = (sc & 0x80) != 0;
        uint8_t k = (uint8_t)(sc & 0x7F);

        // ★e0 必须随手清: 扩展键的释放序列是 0xE0 + 0x8x, 若释放分支
        // 不消费 e0, 下一个普通键会被误当扩展键吃掉 (实测 backspace
        // 丢一个、shift-1 的 '!' 整个丢)
        if (k == SC_LSHIFT || k == SC_RSHIFT) { shift = !release; e0 = 0; continue; }
        if (k == SC_CTRL)   { ctrl = !release; e0 = 0; continue; }
        if (k == SC_CAPS)   { if (!release) caps ^= 1; e0 = 0; continue; }
        if (release)        { e0 = 0; continue; }   // 其余释放码忽略

        // ---- 按下事件 ----
        if (ctrl) {
            if (k == 0x1F) { ed_save(); ed_render(); continue; }     // Ctrl+S
            if (k == 0x2D) {                                        // Ctrl+X
                if (ed_modified && !ed_warn_quit) {
                    ed_warn_quit = 1;
                    k_strcpy(ed_msg, "File modified! Press ^X again to quit without saving");
                    serial_puts("[ED] modified warning\n\r");
                    ed_render();
                    continue;
                }
                break;
            }
            continue;                               // 其他 Ctrl 组合忽略
        }
        ed_warn_quit = 0;

        if (e0) {
            e0 = 0;
            switch (k) {
            case 0x48: ed_up(); break;              // 上
            case 0x50: ed_down(); break;            // 下
            case 0x4B: ed_left(); break;            // 左
            case 0x4D: ed_right(); break;           // 右
            case 0x47: ed_home(); break;            // Home
            case 0x4F: ed_end(); break;             // End
            case 0x49: for (int i = 0; i < ed_view_rows; i++) ed_up(); break;   // PgUp
            case 0x51: for (int i = 0; i < ed_view_rows; i++) ed_down(); break; // PgDn
            case 0x53: ed_delete_at(ed_cur); break; // Delete
            }
            ed_render();
            continue;
        }

        switch (k) {
        case SC_ENTER:
            ed_insert(ed_cur, '\n');
            break;
        case SC_BACKSPACE:
            if (ed_cur > 0) { ed_cur--; ed_delete_at(ed_cur); }
            break;
        case SC_TAB:
            for (int i = 0; i < ED_TAB; i++) {
                if (ed_insert(ed_cur, ' ') < 0) { k_strcpy(ed_msg, "Buffer full (48KB)"); break; }
            }
            break;
        case SC_ESC:
            break;                                  // 教学版忽略 ESC
        default: {
            int is_upper = shift ^ caps;
            char c = is_upper ? keymap_shift[k] : keymap_normal[k];
            if (c >= 32) {
                if (ed_insert(ed_cur, c) < 0) k_strcpy(ed_msg, "Buffer full (48KB)");
            }
        }
        }
        ed_render();
    }

    // 退出: 排空退出键的释放码, 恢复 shell 屏幕
    serial_puts(ed_modified ? "[ED] exit: discarded\n\r" : "[ED] exit: saved\n\r");
    kbd_drain();
    vga_clear();
}
