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
//  键位: 方向键/Home/End/PgUp/PgDn 导航, Ctrl+方向键按词移动,
//  Enter 换行, Backspace/Delete/Ctrl+Backspace 删除, Tab = 4 空格,
//  Ctrl+S 保存, Ctrl+X 退出 (有修改时 Y=保存退出 N=丢弃 Esc=取消),
//  Ctrl+Z/Ctrl+Y 撤销/重做, Ctrl+F 搜索, Ctrl+G 跳行。
//
//  串口标记 (测试预言机): [ED] new/open/saved/save prompt/
//  undo/redo/find/goto/exit(saved/discarded/cancelled)/fail
// ============================================================

#include "editor.h"
#include "fs.h"
#include "vga.h"
#include "keymap.h"
#include "string.h"
#include "serial.h"
#include "shell.h"
#include "keyboard.h"
#include "io.h"
#include "types.h"

#define ED_MAX     49152   // 缓冲上限 48KB (= 文件系统单文件上限)
#define ED_TAB     4       // Tab 展开宽度
#define ED_COLS    80      // 所有字号都是 80 列
#define ED_HISTORY 128     // 撤销/重做操作数上限
#define ED_QUERY   48      // 搜索/跳行输入上限
#define ED_SEQ_INVALID 0xFFFFFFFFu

enum {
    ED_OP_INSERT = 1,
    ED_OP_DELETE = 2,
};

typedef struct {
    uint32_t off;
    uint32_t cur_before;
    uint32_t cur_after;
    uint32_t seq_before;
    uint32_t seq_after;
    uint32_t group;
    char c;
    uint8_t op;
    uint8_t pad[2];
} ed_action_t;

static char ed_buf[ED_MAX];
static uint32_t ed_len;          // 缓冲长度 (字节)
static uint32_t ed_cur;          // 光标 (字节偏移)
static int ed_modified;
static char ed_msg[64];          // 消息行文本
static uint32_t ed_inum;         // 文件 inode
static char ed_path[FS_PATH_MAX];
static int ed_top_line;          // 视口第一行 (垂直滚动)
static int ed_left_col;          // 水平滚动列
static int ed_rows;              // 屏幕行数 (字号决定)
static int ed_view_rows;         // 文本视口行数 = rows - 2
static ed_action_t ed_undo_stack[ED_HISTORY];
static ed_action_t ed_redo_stack[ED_HISTORY];
static int ed_undo_top;
static int ed_redo_top;
static uint32_t ed_state_seq;    // 当前文本状态序号
static uint32_t ed_next_seq;
static uint32_t ed_saved_seq;    // 磁盘内容对应的状态序号
static uint32_t ed_next_group;
static uint32_t ed_active_group;
static char ed_last_query[ED_QUERY];
static uint32_t ed_last_match;

static int ed_poll(void);

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

static uint32_t ed_line_end(int line) {
    uint32_t off = ed_line_start(line);
    while (off < ed_len && ed_buf[off] != '\n') off++;
    return off;
}

static int ed_total_lines(void) {
    int lines = 1;
    for (uint32_t i = 0; i < ed_len; i++) {
        if (ed_buf[i] == '\n') lines++;
    }
    return lines;
}

// ---------- 编辑操作与撤销历史 ----------
static void ed_break_group(void) { ed_active_group = 0; }

static uint32_t ed_action_group(void) {
    if (!ed_active_group) {
        ed_active_group = ed_next_group++;
        if (!ed_next_group) ed_next_group = 1;
    }
    return ed_active_group;
}

static void ed_update_modified(void) {
    ed_modified = ed_saved_seq == ED_SEQ_INVALID ||
                  ed_state_seq != ed_saved_seq;
}

static int ed_insert_raw(uint32_t off, char c) {
    if (ed_len >= ED_MAX || off > ed_len) return -1;
    for (uint32_t i = ed_len; i > off; i--) ed_buf[i] = ed_buf[i - 1];
    ed_buf[off] = c;
    ed_len++;
    return 0;
}

static int ed_delete_raw(uint32_t off) {
    if (off >= ed_len) return -1;
    for (uint32_t i = off; i < ed_len - 1; i++) ed_buf[i] = ed_buf[i + 1];
    ed_len--;
    return 0;
}

static void ed_push_undo(const ed_action_t* action) {
    if (ed_undo_top >= ED_HISTORY) {
        for (int i = 1; i < ED_HISTORY; i++) ed_undo_stack[i - 1] = ed_undo_stack[i];
        ed_undo_top--;
        ed_saved_seq = ED_SEQ_INVALID;
    }
    ed_undo_stack[ed_undo_top++] = *action;
}

static void ed_record(uint8_t op, uint32_t off, char c,
                      uint32_t cur_before, uint32_t cur_after, uint32_t group) {
    if (ed_redo_top) {
        ed_redo_top = 0;
        ed_saved_seq = ED_SEQ_INVALID;
    }

    ed_action_t action;
    action.off = off;
    action.cur_before = cur_before;
    action.cur_after = cur_after;
    action.seq_before = ed_state_seq;
    action.seq_after = ed_next_seq++;
    action.group = group;
    action.c = c;
    action.op = op;

    ed_push_undo(&action);
    ed_state_seq = action.seq_after;
    ed_cur = cur_after;
    ed_update_modified();
}

static int ed_insert_action(char c) {
    uint32_t off = ed_cur;
    uint32_t group = ed_action_group();
    if (ed_insert_raw(off, c) < 0) return -1;
    ed_record(ED_OP_INSERT, off, c, off, off + 1, group);
    return 0;
}

static int ed_delete_action(uint32_t off) {
    uint32_t cur_before = ed_cur;
    if (off >= ed_len) return -1;
    char c = ed_buf[off];
    uint32_t cur_after = cur_before > off ? cur_before - 1 : cur_before;
    if (ed_delete_raw(off) < 0) return -1;
    ed_record(ED_OP_DELETE, off, c, cur_before, cur_after, ed_action_group());
    return 0;
}

static void ed_delete_range(uint32_t from, uint32_t to) {
    if (from >= to) return;
    uint32_t count = to - from;
    for (uint32_t i = 0; i < count; i++) {
        if (ed_delete_action(from) < 0) break;
    }
}

static void ed_undo(void) {
    ed_break_group();
    if (!ed_undo_top) {
        k_strcpy(ed_msg, "Nothing to undo");
        return;
    }

    uint32_t group = ed_undo_stack[ed_undo_top - 1].group;
    while (ed_undo_top && ed_undo_stack[ed_undo_top - 1].group == group) {
        ed_action_t action = ed_undo_stack[--ed_undo_top];
        if (action.op == ED_OP_INSERT) ed_delete_raw(action.off);
        else ed_insert_raw(action.off, action.c);
        ed_cur = action.cur_before;
        ed_state_seq = action.seq_before;
        if (ed_redo_top < ED_HISTORY) ed_redo_stack[ed_redo_top++] = action;
    }
    ed_update_modified();
    k_strcpy(ed_msg, "Undo");
    serial_puts("[ED] undo\n\r");
}

static void ed_redo(void) {
    ed_break_group();
    if (!ed_redo_top) {
        k_strcpy(ed_msg, "Nothing to redo");
        return;
    }

    uint32_t group = ed_redo_stack[ed_redo_top - 1].group;
    while (ed_redo_top && ed_redo_stack[ed_redo_top - 1].group == group) {
        ed_action_t action = ed_redo_stack[--ed_redo_top];
        if (action.op == ED_OP_INSERT) ed_insert_raw(action.off, action.c);
        else ed_delete_raw(action.off);
        ed_cur = action.cur_after;
        ed_state_seq = action.seq_after;
        ed_push_undo(&action);
    }
    ed_update_modified();
    k_strcpy(ed_msg, "Redo");
    serial_puts("[ED] redo\n\r");
}

// ---------- 光标移动 ----------
static void ed_left(void)  { if (ed_cur > 0) ed_cur--; }
static void ed_right(void) { if (ed_cur < ed_len) ed_cur++; }
static void ed_home(void)  { ed_cur = ed_cur_line_start(); }
static void ed_end(void) {
    int line, col;
    ed_pos(ed_cur, &line, &col);
    ed_cur = ed_line_end(line);
}

static int ed_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n';
}

static void ed_prev_word(void) {
    while (ed_cur > 0 && ed_is_space(ed_buf[ed_cur - 1])) ed_cur--;
    while (ed_cur > 0 && !ed_is_space(ed_buf[ed_cur - 1])) ed_cur--;
}

static void ed_next_word(void) {
    while (ed_cur < ed_len && !ed_is_space(ed_buf[ed_cur])) ed_cur++;
    while (ed_cur < ed_len && ed_is_space(ed_buf[ed_cur])) ed_cur++;
}

static void ed_up(void) {
    int line, col;
    ed_pos(ed_cur, &line, &col);
    if (line == 0) return;
    uint32_t ls = ed_line_start(line - 1);
    uint32_t le = ed_line_end(line - 1);
    uint32_t target = ls + (uint32_t)col;
    ed_cur = target < le ? target : le;
}

static void ed_down(void) {
    int line, col;
    ed_pos(ed_cur, &line, &col);
    if (line + 1 >= ed_total_lines()) return;
    uint32_t ls = ed_line_start(line + 1);
    uint32_t le = ed_line_end(line + 1);
    uint32_t target = ls + (uint32_t)col;
    ed_cur = target < le ? target : le;
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
    for (int y = 0; y < ed_view_rows; y++) {
        int screen_line = ed_top_line + y;
        uint32_t line_off = ed_line_start(screen_line);
        uint32_t end = ed_line_end(screen_line);
        for (int x = 0; x < ED_COLS; x++) {
            uint32_t src = line_off + (uint32_t)(ed_left_col + x);
            char c = (src < end) ? ed_buf[src] : ' ';
            int is_cursor = screen_line == line && x == col - ed_left_col;
            vga_putc_attr_xy(x, y, c, is_cursor ? 0x70 : 0x0F);
        }
    }

    // 2. 状态栏 (反显): 快捷键 | 行列 | 路径 [M]
    static char status[ED_COLS + 1];
    k_memset(status, ' ', ED_COLS);
    int pos = 0;
    const char* s = " ^S Save ^X Exit ^Z Undo ^F Find ^G Goto | L";
    while (*s && pos < ED_COLS - 4) status[pos++] = *s++;
    char num[12];
    ed_uint_to_str((uint32_t)line + 1, num);
    s = num; while (*s && pos < ED_COLS - 4) status[pos++] = *s++;
    if (pos < ED_COLS - 4) status[pos++] = ' ';
    if (pos < ED_COLS - 4) status[pos++] = 'C';
    ed_uint_to_str((uint32_t)col + 1, num);
    s = num; while (*s && pos < ED_COLS - 4) status[pos++] = *s++;
    if (pos < ED_COLS - 4) status[pos++] = ' ';
    if (pos < ED_COLS - 4) status[pos++] = '|';
    if (pos < ED_COLS - 4) status[pos++] = ' ';
    s = ed_path;
    while (*s && pos < ED_COLS - 4) status[pos++] = *s++;
    if (ed_modified && pos <= ED_COLS - 4) {
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

// ---------- 搜索 / 跳行输入 ----------
static void ed_prompt_text(const char* prompt, const char* input) {
    int pos = 0;
    while (*prompt && pos < (int)sizeof(ed_msg) - 1) ed_msg[pos++] = *prompt++;
    while (*input && pos < (int)sizeof(ed_msg) - 1) ed_msg[pos++] = *input++;
    ed_msg[pos] = '\0';
}

static int ed_read_prompt(const char* prompt, char* input, uint32_t cap,
                          int digits_only, int* shift, int* ctrl,
                          int* caps, int* e0) {
    uint32_t len = k_strlen(input);
    ed_prompt_text(prompt, input);
    ed_render();

    while (1) {
        int sc = ed_poll();
        if (sc < 0) { __asm__ volatile("hlt"); continue; }
        if (sc == 0xE0) { *e0 = 1; continue; }

        int release = (sc & 0x80) != 0;
        uint8_t k = (uint8_t)(sc & 0x7F);
        if (k == SC_LSHIFT || k == SC_RSHIFT) { *shift = !release; *e0 = 0; continue; }
        if (k == SC_CTRL) { *ctrl = !release; *e0 = 0; continue; }
        if (k == SC_CAPS) { if (!release) *caps ^= 1; *e0 = 0; continue; }
        if (release) { *e0 = 0; continue; }
        if (*e0) { *e0 = 0; continue; }

        if (k == SC_ESC) return 0;
        if (k == SC_ENTER) return 1;
        if (k == SC_BACKSPACE) {
            if (len) input[--len] = '\0';
            ed_prompt_text(prompt, input);
            ed_render();
            continue;
        }

        int upper = *shift ^ *caps;
        char c = upper ? keymap_shift[k] : keymap_normal[k];
        if (c < 32 || (digits_only && (c < '0' || c > '9'))) continue;
        if (len + 1 < cap) {
            input[len++] = c;
            input[len] = '\0';
            ed_prompt_text(prompt, input);
            ed_render();
        }
    }
}

static void ed_goto_line(int* shift, int* ctrl, int* caps, int* e0) {
    char input[ED_QUERY];
    input[0] = '\0';
    if (!ed_read_prompt("Goto line: ", input, sizeof(input), 1,
                        shift, ctrl, caps, e0) || !input[0]) {
        k_strcpy(ed_msg, "Goto cancelled");
        return;
    }

    uint32_t requested = 0;
    for (uint32_t i = 0; input[i]; i++) {
        if (requested > 999999u) break;
        requested = requested * 10u + (uint32_t)(input[i] - '0');
    }
    if (!requested) requested = 1;
    int lines = ed_total_lines();
    if (requested > (uint32_t)lines) requested = (uint32_t)lines;
    ed_cur = ed_line_start((int)requested - 1);
    k_strcpy(ed_msg, "Line ");
    ed_uint_to_str(requested, ed_msg + k_strlen(ed_msg));
    serial_puts("[ED] goto\n\r");
}

static void ed_find_next(int* shift, int* ctrl, int* caps, int* e0) {
    char query[ED_QUERY];
    query[0] = '\0';
    if (!ed_read_prompt("Find: ", query, sizeof(query), 0,
                        shift, ctrl, caps, e0) || !query[0]) {
        k_strcpy(ed_msg, "Find cancelled");
        return;
    }

    uint32_t qlen = k_strlen(query);
    if (qlen > ed_len) {
        k_strcpy(ed_msg, "Not found");
        serial_puts("[ED] find miss\n\r");
        return;
    }

    uint32_t start = ed_cur;
    int same_query = k_strcmp(query, ed_last_query) == 0;
    if (same_query && ed_cur == ed_last_match && start < ed_len) start++;
    if (start >= ed_len) start = 0;

    for (uint32_t delta = 0; delta < ed_len; delta++) {
        uint32_t at = (start + delta) % ed_len;
        if (at + qlen > ed_len) continue;
        int match = 1;
        for (uint32_t i = 0; i < qlen; i++) {
            if (ed_buf[at + i] != query[i]) { match = 0; break; }
        }
        if (match) {
            ed_cur = at;
            ed_last_match = at;
            k_strcpy(ed_last_query, query);
            int line, col;
            ed_pos(ed_cur, &line, &col);
            k_strcpy(ed_msg, "Found L");
            ed_uint_to_str((uint32_t)line + 1, ed_msg + k_strlen(ed_msg));
            k_strcpy(ed_msg + k_strlen(ed_msg), " C");
            ed_uint_to_str((uint32_t)col + 1, ed_msg + k_strlen(ed_msg));
            serial_puts("[ED] find hit\n\r");
            return;
        }
    }

    k_strcpy(ed_msg, "Not found");
    serial_puts("[ED] find miss\n\r");
}

// ---------- 保存 ----------
static int ed_save(void) {
    int r = fs_write(ed_inum, ed_buf, ed_len, 0);   // 截断写
    if (r < 0) {
        k_strcpy(ed_msg, "Save failed!");
        serial_puts("[ED] save fail\n\r");
        return -1;
    }
    ed_saved_seq = ed_state_seq;
    ed_update_modified();
    k_strcpy(ed_msg, "Saved ");
    ed_uint_to_str((uint32_t)r, ed_msg + k_strlen(ed_msg));
    k_strcpy(ed_msg + k_strlen(ed_msg), " bytes");
    serial_puts("[ED] saved: "); serial_puts(ed_path);
    serial_puts(" ("); serial_put_hex((uint32_t)r); serial_puts(" bytes)\n\r");
    return 0;
}

// ---------- 按键轮询 (里程碑 11a: 键盘环形缓冲) ----------
// 老版直接轮询 8042 (编辑器跑在 IRQ1 处理链里的年代);
// 现在 IRQ1 处理器独占 8042 并把扫描码推进环形缓冲, 这里消费。
// 原始字节语义不变: 0xE0 前缀 / 按下 <0x80 / 释放 >=0x80,
// 编辑器自己的 Shift/Ctrl/Caps/0xE0 跟踪逻辑原样保留。
static int ed_poll(void) {
    return kbd_pop();
}

// ---------- 主入口 ----------
void editor_run(const char* path) {
    k_strcpy(ed_path, path);
    ed_len = 0; ed_cur = 0; ed_modified = 0;
    ed_msg[0] = '\0';
    ed_top_line = 0; ed_left_col = 0;
    ed_undo_top = 0; ed_redo_top = 0;
    ed_state_seq = 0; ed_next_seq = 1; ed_saved_seq = 0;
    ed_next_group = 1; ed_active_group = 0;
    ed_last_query[0] = '\0'; ed_last_match = ED_SEQ_INVALID;
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
        if (e0) {
            e0 = 0;
            ed_break_group();
            switch (k) {
            case 0x48: ed_up(); break;              // 上
            case 0x50: ed_down(); break;            // 下
            case 0x4B:
                if (ctrl) {
                    ed_prev_word();
                    serial_puts("[ED] word-left\n\r");
                } else ed_left();
                break;
            case 0x4D:
                if (ctrl) {
                    ed_next_word();
                    serial_puts("[ED] word-right\n\r");
                } else ed_right();
                break;
            case 0x47:
                if (ctrl) ed_cur = 0; else ed_home();
                break;
            case 0x4F:
                if (ctrl) ed_cur = ed_len; else ed_end();
                break;
            case 0x49: for (int i = 0; i < ed_view_rows; i++) ed_up(); break;   // PgUp
            case 0x51: for (int i = 0; i < ed_view_rows; i++) ed_down(); break; // PgDn
            case 0x53:
                if (ctrl) {
                    uint32_t start = ed_cur;
                    ed_next_word();
                    uint32_t end = ed_cur;
                    ed_cur = start;
                    ed_delete_range(start, end);
                    serial_puts("[ED] delete-word-right\n\r");
                } else {
                    ed_delete_action(ed_cur);
                }
                break;
            }
            ed_render();
            continue;
        }

        if (ctrl) {
            if (k == 0x1F) {                                        // Ctrl+S
                ed_break_group();
                ed_save();
            } else if (k == 0x2D) {                                 // Ctrl+X
                ed_break_group();
                if (!ed_modified) break;                            // 未修改 → 直接退出
                k_strcpy(ed_msg, "Save before exit? (Y=save+exit  N=discard  Esc=cancel)");
                serial_puts("[ED] save prompt\n\r");
                ed_render();

                int save_exit = 0, discard_exit = 0, done = 0;
                while (!done) {
                    int sc2 = ed_poll();
                    if (sc2 < 0) { __asm__ volatile("hlt"); continue; }
                    if (sc2 == 0xE0) { e0 = 1; continue; }
                    int rel2 = (sc2 & 0x80) != 0;
                    uint8_t k2 = (uint8_t)(sc2 & 0x7F);
                    if (k2 == SC_LSHIFT || k2 == SC_RSHIFT) {
                        shift = !rel2; e0 = 0; continue;
                    }
                    if (k2 == SC_CTRL) { ctrl = !rel2; e0 = 0; continue; }
                    if (k2 == SC_CAPS) { if (!rel2) caps ^= 1; e0 = 0; continue; }
                    if (rel2) { e0 = 0; continue; }
                    if (e0) { e0 = 0; continue; }
                    char ans = (shift ^ caps) ? keymap_shift[k2] : keymap_normal[k2];
                    if (ans == 'y' || ans == 'Y') { save_exit = 1; done = 1; }
                    else if (ans == 'n' || ans == 'N') { discard_exit = 1; done = 1; }
                    else {
                        k_strcpy(ed_msg, "Cancelled");
                        serial_puts("[ED] exit: cancelled\n\r");
                        ed_render();
                        done = 1;
                    }
                }
                if (save_exit && ed_save() == 0) break;
                if (discard_exit) break;
                ed_render();
                continue;
            } else if (k == 0x2C) {                                 // Ctrl+Z
                ed_undo();
            } else if (k == 0x15) {                                 // Ctrl+Y
                ed_redo();
            } else if (k == 0x21) {                                 // Ctrl+F
                ed_break_group();
                ed_find_next(&shift, &ctrl, &caps, &e0);
            } else if (k == 0x22) {                                 // Ctrl+G
                ed_break_group();
                ed_goto_line(&shift, &ctrl, &caps, &e0);
            } else if (k == SC_BACKSPACE) {                         // Ctrl+Backspace
                ed_break_group();
                uint32_t stop = ed_cur;
                ed_prev_word();
                ed_delete_range(ed_cur, stop);
                serial_puts("[ED] delete-word-left\n\r");
                ed_break_group();
            }
            ed_render();
            continue;
        }

        switch (k) {
        case SC_ENTER:
            ed_break_group();
            if (ed_insert_action('\n') < 0) k_strcpy(ed_msg, "Buffer full (48KB)");
            ed_break_group();
            break;
        case SC_BACKSPACE:
            ed_break_group();
            if (ed_cur > 0) ed_delete_action(ed_cur - 1);
            ed_break_group();
            break;
        case SC_TAB:
            ed_break_group();
            for (int i = 0; i < ED_TAB; i++) {
                if (ed_insert_action(' ') < 0) {
                    k_strcpy(ed_msg, "Buffer full (48KB)");
                    break;
                }
            }
            ed_break_group();
            break;
        case SC_ESC:
            ed_break_group();
            break;
        default: {
            int is_upper = shift ^ caps;
            char c = is_upper ? keymap_shift[k] : keymap_normal[k];
            if (c >= 32) {
                if (ed_insert_action(c) < 0) k_strcpy(ed_msg, "Buffer full (48KB)");
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
