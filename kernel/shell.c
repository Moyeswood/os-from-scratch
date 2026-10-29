#include "shell.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "io.h"
#include "types.h"
#include "task.h"
#include "realmode.h"
#include "graphics.h"
#include "tetris.h"
#include "mario.h"
#include "font.h"
#include "fs.h"
#include "string.h"
#include "editor.h"
#include "cc.h"
#include "splash.h"

#define MAX_INPUT 256
#define MAX_HISTORY 10

static char input_buffer[MAX_INPUT];
static int input_index = 0;


// 命令历史记录
static char history[MAX_HISTORY][MAX_INPUT];
static int history_count = 0;
static int history_pos = 0; 

// 简单的字符串工具函数
static int shell_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static int shell_strlen(const char* s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static void shell_strcpy(char* dst, const char* src) {
    while (*src) { *dst++ = *src++; }
    *dst = '\0';
}

// 10 进制打印 (文件大小/统计用)
static void vga_put_dec(uint32_t n) {
    if (n == 0) { vga_putc('0'); return; }
    char tmp[12];
    int pos = 0;
    while (n) {
        tmp[pos++] = (char)('0' + n % 10);
        n /= 10;
    }
    while (pos) vga_putc(tmp[--pos]);
}

// 把输入按空格拆成 argv (就地修改: 空格→'\0'), 返回 argc
static int shell_tokenize(char* input, char** argv, int max) {
    int argc = 0;
    while (*input && argc < max) {
        while (*input == ' ') input++;
        if (*input == '\0') break;
        argv[argc++] = input;
        while (*input && *input != ' ') input++;
        if (*input) *input++ = '\0';
    }
    return argc;
}

// 提示符: OS:<当前目录>>
static void shell_prompt(void) {
    static char pwd[FS_PATH_MAX];
    fs_pwd(pwd);
    vga_puts("OS:"); vga_puts(pwd); vga_puts("> ");
}

// 【关键修复】：用纯数学计算，彻底消灭局部数组，防止栈溢出崩溃！
static void vga_put_hex(uint32_t num) {
    vga_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (num >> i) & 0xF;
        char c;
        if (nibble < 10) {
            c = '0' + nibble;
        } else {
            c = 'A' + nibble - 10;
        }
        vga_putc(c);
    }
}

// 辅助：BCD 转二进制 (CMOS 时钟使用 BCD 编码)
static uint8_t bcd_to_bin(uint8_t val) {
    return (val / 16) * 10 + (val % 16);
}

// 辅助：读取 CMOS 寄存器
static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

// 【关键】排空 8042 输出缓冲区: 触发命令的那个键, 它的"释放码"一定会
// 晚到并残留在缓冲区里, 不排空的话会污染接下来的"等按键"逻辑。
// 所以: 清空 → 等释放码到达 → 还有字节就再清, 直到缓冲区安静。
void kbd_drain() {
    do {
        while (inb(0x64) & 1) { inb(0x60); }           // 清空输出缓冲区
        for (volatile int i = 0; i < 20000000; i++);   // 给释放码留到达时间
    } while (inb(0x64) & 1);                           // 又来了字节 → 再清一轮
}

// 重新打印当前输入行（用于历史记录切换）
static void redraw_input_line() {
    vga_putc('\r');
    shell_prompt();
    for (int i = 0; i < input_index; i++) {
        vga_putc(input_buffer[i]);
    }
    // 用空格清除该行剩余的旧字符
    for (int i = input_index; i < MAX_INPUT - 10; i++) {
        vga_putc(' ');
    }
    vga_putc('\r');
    shell_prompt();
    for (int i = 0; i < input_index; i++) {
        vga_putc(input_buffer[i]);
    }
}

// 1. help 命令
static void cmd_help() {
    vga_puts("\n\rAvailable commands:\n\r");
    vga_puts("  help     - Show this help message\n\r");
    vga_puts("  clear    - Clear the screen\n\r");
    vga_puts("  meminfo  - Show memory information\n\r");
    vga_puts("  echo [t] - Print text, or: echo text > file\n\r");
    vga_puts("  date     - Show current time (CMOS)\n\r");
    vga_puts("  reboot   - Reboot the system\n\r");
    vga_puts("  version  - Show OS version\n\r");
    vga_puts("  ps       - Show running tasks\n\r");
    vga_puts("  start    - Start multitasking demo\n\r");
    vga_puts("  stop     - Stop multitasking demo\n\r");
    vga_puts("  testmode - Test graphics mode round trip (BIOS INT 10h)\n\r");
    vga_puts("  tetris   - Play Tetris (ESC to exit)\n\r");
    vga_puts("  mario    - Play Super Mario (ESC to exit)\n\r");
    vga_puts("  font N   - Change font size: 12 (33 lines), 14 (28), 16 (25)\n\r");
    vga_puts("  pwd      - Print working directory\n\r");
    vga_puts("  ls [d]   - List directory\n\r");
    vga_puts("  cd [d]   - Change directory (no arg = root)\n\r");
    vga_puts("  mkdir d  - Create directory\n\r");
    vga_puts("  touch f  - Create empty file\n\r");
    vga_puts("  cat f    - Print file contents\n\r");
    vga_puts("  rm p     - Remove file or empty directory\n\r");
    vga_puts("  df       - Show filesystem usage\n\r");
    vga_puts("  edit f   - Edit file (^S save, ^X exit)\n\r");
    vga_puts("  cc f.c   - Compile and run C program (c4 subset)\n\r");
    vga_puts("  splash   - Replay the boot screen animation\n\r");
}

// 2. date 命令 (读取 CMOS)
static void cmd_date() {
    uint8_t sec = bcd_to_bin(cmos_read(0x00));
    uint8_t min = bcd_to_bin(cmos_read(0x02));
    uint8_t hour = bcd_to_bin(cmos_read(0x04));
    
    vga_puts("\n\rCurrent Time: ");
    vga_putc('0' + (hour / 10)); vga_putc('0' + (hour % 10)); vga_puts(":");
    vga_putc('0' + (min / 10)); vga_putc('0' + (min % 10)); vga_puts(":");
    vga_putc('0' + (sec / 10)); vga_putc('0' + (sec % 10));
    vga_puts("\n\r");
}

// 4. reboot 命令
static void cmd_reboot() {
    vga_puts("\n\rRebooting system...\n\r");
    outb(0x64, 0xFE);
    while(1) { __asm__ volatile("hlt"); }
}

// 5. version 命令
static void cmd_version() {
    vga_puts("\n\rMyQwen-OS v1.0.0 - Built with Qwen\n\r");
}

// font 命令: 切换字号 (12/14/16 → 33/28/25 行)
static void cmd_font(int argc, char** argv) {
    uint8_t h = 0;
    if (argc > 1) {
        const char* p = argv[1];
        while (*p >= '0' && *p <= '9') {
            h = (uint8_t)(h * 10 + (*p - '0'));
            p++;
        }
    }
    if (h == 0) {
        // 无参数: 显示当前字号
        uint8_t cur = font_current();
        vga_puts("\n\rCurrent font size: ");
        vga_putc('0' + cur / 10);
        vga_putc('0' + cur % 10);
        vga_puts(" (");
        int rows = vga_get_rows();
        vga_putc('0' + rows / 10);
        vga_putc('0' + rows % 10);
        vga_puts(" lines)\n\r");
        vga_puts("Usage: font 12 | 14 | 16\n\r");
    } else if (h == 12 || h == 14 || h == 16) {
        font_apply(h);              // 加载字体 + 自测 (串口报 [FONT] OK/FAIL)
        vga_puts("MyQwen-OS v1.0.0\n\r");
    } else {
        vga_puts("\n\rUsage: font 12 | 14 | 16\n\r");
    }
}

// ============================================================
//  文件系统命令
// ============================================================

static void cmd_pwd() {
    char pwd[FS_PATH_MAX];
    fs_pwd(pwd);
    serial_puts("[SH] pwd: "); serial_puts(pwd); serial_puts("\n\r");
    vga_puts("\n\r");
    vga_puts(pwd);
    vga_puts("\n\r");
}

static void cmd_ls(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : ".";
    int inum = fs_lookup(path);
    if (inum < 0) {
        vga_puts("\n\rls: No such file or directory\n\r");
        return;
    }
    char name[32];
    uint32_t entry_inum, type, size;
    if (fs_readdir((uint32_t)inum, 0, name, &entry_inum, &type, &size) < 0) {
        vga_puts("\n\rls: Not a directory\n\r");
        return;
    }
    vga_puts("\n\r");
    for (uint32_t idx = 0; ; idx++) {
        int r = fs_readdir((uint32_t)inum, idx, name, &entry_inum, &type, &size);
        if (r <= 0) break;
        if (entry_inum == 0) continue;   // 删除留下的空槽
        if (k_strcmp(name, ".") == 0 || k_strcmp(name, "..") == 0) continue;
        vga_puts("  ");
        vga_puts(name);
        for (uint32_t pad = k_strlen(name); pad < 20; pad++) vga_putc(' ');
        serial_puts("[SH] ls: "); serial_puts(name);
        serial_puts(type == FS_DIR ? " (DIR)\n\r" : "\n\r");
        if (type == FS_DIR) {
            vga_puts("<DIR>\n\r");
        } else {
            vga_put_dec(size);
            vga_puts(" bytes\n\r");
        }
    }
}

static void cmd_cd(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "/";
    if (fs_chdir(path) < 0) {
        serial_puts("[SH] cd fail\n\r");
        vga_puts("\n\rcd: No such directory\n\r");
        return;
    }
    char pwd[FS_PATH_MAX];
    fs_pwd(pwd);
    serial_puts("[SH] cwd: "); serial_puts(pwd); serial_puts("\n\r");
}

static void cmd_mkdir(int argc, char** argv) {
    if (argc < 2) {
        vga_puts("\n\rUsage: mkdir <path>\n\r");
        return;
    }
    if (fs_create(argv[1], FS_DIR) < 0) {
        serial_puts("[SH] mkdir fail\n\r");
        vga_puts("\n\rmkdir: failed (exists? parent missing? name too long?)\n\r");
    } else {
        serial_puts("[SH] mkdir ok\n\r");
    }
}

static void cmd_touch(int argc, char** argv) {
    if (argc < 2) {
        vga_puts("\n\rUsage: touch <path>\n\r");
        return;
    }
    // 已存在就什么都不做 (真 touch 行为), 不存在则创建空文件
    if (fs_lookup(argv[1]) < 0 && fs_create(argv[1], FS_FILE) < 0) {
        serial_puts("[SH] touch fail\n\r");
        vga_puts("\n\rtouch: failed (parent missing? name too long?)\n\r");
    } else {
        serial_puts("[SH] touch ok\n\r");
    }
}

static void cmd_cat(int argc, char** argv) {
    if (argc < 2) {
        vga_puts("\n\rUsage: cat <path>\n\r");
        return;
    }
    int inum = fs_lookup(argv[1]);
    if (inum < 0) {
        serial_puts("[SH] cat fail\n\r");
        vga_puts("\n\rcat: No such file or directory\n\r");
        return;
    }
    static char buf[512];
    vga_puts("\n\r");
    serial_puts("[SH] cat: ");
    uint32_t off = 0;
    int r;
    while ((r = fs_read((uint32_t)inum, buf, off, sizeof(buf))) > 0) {
        for (int i = 0; i < r; i++) vga_putc(buf[i]);
        for (int i = 0; i < r; i++) serial_putc(buf[i]);
        off += (uint32_t)r;
    }
    serial_puts("\n\r");
    if (r < 0) {
        vga_puts("cat: Is a directory\n\r");
    }
    vga_puts("\n\r");
}

static void cmd_rm(int argc, char** argv) {
    if (argc < 2) {
        vga_puts("\n\rUsage: rm <path>\n\r");
        return;
    }
    if (fs_remove(argv[1]) < 0) {
        serial_puts("[SH] rm fail\n\r");
        vga_puts("\n\rrm: failed (missing? dir not empty? cwd or root?)\n\r");
    } else {
        serial_puts("[SH] rm ok\n\r");
    }
}

// echo text... [> file] — 有 > 时把文字写进文件 (不存在则创建, 覆盖写)
static void cmd_echo(int argc, char** argv) {
    int gt = -1;
    for (int i = 1; i < argc; i++) {
        if (k_strcmp(argv[i], ">") == 0) { gt = i; break; }
    }
    int inum = -1;
    if (gt > 0 && gt + 1 < argc) {
        inum = fs_lookup(argv[gt + 1]);
        if (inum < 0) inum = fs_create(argv[gt + 1], FS_FILE);
        if (inum < 0) vga_puts("\n\recho: cannot create file\n\r");
    }
    vga_puts("\n\r");
    int first_word = 1;
    for (int i = 1; i < (gt > 0 ? gt : argc); i++) {
        if (!first_word) vga_putc(' ');
        first_word = 0;
        vga_puts(argv[i]);
        if (inum >= 0) {
            if (i > 1) fs_write((uint32_t)inum, " ", 1, 1);
            // 第一个词用截断写 (覆盖旧内容), 之后追加
            fs_write((uint32_t)inum, argv[i], k_strlen(argv[i]), (i == 1) ? 0 : 1);
        }
    }
    if (inum >= 0) {
        if (first_word) fs_write((uint32_t)inum, "", 0, 0);  // echo > file: 清空
        fs_write((uint32_t)inum, "\n", 1, 1);
        serial_puts("[SH] echo ok\n\r");
    } else {
        vga_puts("\n\r");
    }
}

static void cmd_df() {
    uint32_t fb, fi, tb, ti;
    fs_stat(&fb, &fi, &tb, &ti);
    vga_puts("\n\rFilesystem: ");
    vga_put_dec(tb * 4);
    vga_puts(" KB, ");
    vga_put_dec(tb);
    vga_puts(" blocks x 4KB, ");
    vga_put_dec(ti);
    vga_puts(" inodes\n\r");
    vga_puts("Free: ");
    vga_put_dec(fb);
    vga_puts(" blocks (");
    vga_put_dec(fb * 4);
    vga_puts(" KB), ");
    vga_put_dec(fi);
    vga_puts(" inodes\n\r");
    serial_puts("[SH] df: free "); serial_put_hex(fb);
    serial_puts(" blocks, "); serial_put_hex(fi); serial_puts(" inodes\n\r");
}

// 命令分发器
static void execute_command() {
    input_buffer[input_index] = '\0';
    vga_puts("\n\r");

    char* argv[16];
    int argc = shell_tokenize(input_buffer, argv, 16);

    if (argc == 0) {
        // 空命令
    } else if (k_strcmp(argv[0], "help") == 0) {
        cmd_help();
    } else if (k_strcmp(argv[0], "clear") == 0) {
        vga_clear();
    } else if (k_strcmp(argv[0], "meminfo") == 0) {
        uint32_t total = get_total_pages();
        uint32_t used = get_used_pages();
        vga_puts("Memory: Total="); vga_put_hex(total);
        vga_puts(", Used="); vga_put_hex(used);
        vga_puts(", Free="); vga_put_hex(total - used); vga_puts("\n\r");
    } else if (k_strcmp(argv[0], "version") == 0) {
        cmd_version();
    } else if (k_strcmp(argv[0], "reboot") == 0) {
        cmd_reboot();
    } else if (k_strcmp(argv[0], "start") == 0) {
        multitasking_enabled = 1;
        vga_puts("\n\rMultitasking STARTED. You will see A, B and C.\n\r");
        vga_puts("(C is a busy-loop task that never yields - only\n\r");
        vga_puts(" preemptive scheduling can interrupt it.)\n\r");
    } else if (k_strcmp(argv[0], "stop") == 0) {
        multitasking_enabled = 0;
        vga_puts("\n\rMultitasking STOPPED.\n\r");
    } else if (k_strcmp(argv[0], "ps") == 0) {
        task_print_status();
    } else if (k_strcmp(argv[0], "testmode") == 0) {
        vga_puts("\n\r[RM] Switching to mode 13h (320x200x256)...\n\r");

        // 实模式往返: 调 BIOS 切图形模式, 自动回到保护模式
        rm_switch_mode(0x13);
        serial_puts("[RM] Mode 13h set, back in protected mode!\n\r");

        gfx_demo();   // 图形库自测: 调色板/渐变/矩形/字体/精灵
        serial_puts("[RM] Press any key to return to text mode...\n\r");

        // 【关键】等待一个"新鲜"的按键。
        // 触发本命令的回车键, 它的"释放码"可能会晚到 (8042 输出缓冲区排队),
        // 直接等待会把它误当成"按任意键", 图形模式一闪而过。
        // 所以: 清空 → 等释放码到达 → 还有字节就再清, 直到缓冲区安静。
        do {
            while (inb(0x64) & 1) { inb(0x60); }   // 清空 8042 输出缓冲区
            for (volatile int i = 0; i < 20000000; i++);  // 给释放码留到达时间
        } while (inb(0x64) & 1);                   // 又来了字节 → 再清一轮

        // 现在等真正的下一个按键 (轮询 8042 状态端口 bit0 = 缓冲区有数据)。
        // ★只接受"按下码" (<0x80): 残留的释放码 (回车/之前按键) 不算数,
        // 否则释放码会被误当"任意键"提前切回文本模式。
        while (1) {
            if (inb(0x64) & 1) {
                uint8_t sc = inb(0x60);   // 读走扫描码
                if (sc < 0x80) break;     // 按下码 → 继续
            }
        }

        // ★【关键】再排空一次: 该按键的 IRQ1 在 PIC 里挂起 (触发本命令的
        // 回车处理器还没 EOI), EOI 后 IRQ1 会带着残留/重发的按下码再次
        // 投递, 键盘处理器会把重复码送进 shell 输入缓冲污染下一条命令
        // (实测: "font 12" 变成 "afont 12" → Unknown command)。
        kbd_drain();

        // 第二次往返: 切回文本模式
        rm_switch_mode(0x03);
        serial_puts("[RM] Text mode 03h restored, round trip OK!\n\r");

        // 切模式会重载 BIOS ROM 字体 + 25 行, 重新应用用户当前字号
        font_apply(font_current());

        vga_clear();
        vga_puts("MyQwen-OS v1.0.0\n\r");
        vga_puts("[RM] Round-trip test PASSED!\n\r");
    } else if (k_strcmp(argv[0], "tetris") == 0) {
        vga_puts("\n\r[GAME] Entering TETRIS... (ESC to exit)\n\r");

        // 切到模式 13h, 跑游戏, 再切回文本模式
        rm_switch_mode(0x13);
        serial_puts("[GAME] Mode 13h set, starting tetris...\n\r");
        gfx_init();     // 默认调色板 + 清屏 (游戏会再设自己的配色)
        tetris_run();
        serial_puts("[GAME] Tetris exited\n\r");

        // 【关键】排空 8042: 退出游戏的 ESC 键, 它的"释放码"还在缓冲区里,
        // 不排空的话会留到下一个"等按键"逻辑里被误读 (testmode 同款坑)
        kbd_drain();

        rm_switch_mode(0x03);
        serial_puts("[GAME] Text mode restored\n\r");

        // 切回模式 03h 会重载 BIOS ROM 字体 + 25 行, 重新应用用户字号
        font_apply(font_current());

        vga_clear();
        vga_puts("MyQwen-OS v1.0.0\n\r");
    } else if (k_strcmp(argv[0], "mario") == 0) {
        vga_puts("\n\r[GAME] Entering SUPER MARIO... (ESC to exit)\n\r");

        // 切到模式 13h, 跑游戏, 再切回文本模式 (和 tetris 同一套流程)
        rm_switch_mode(0x13);
        serial_puts("[GAME] Mode 13h set, starting mario...\n\r");
        gfx_init();     // 默认调色板 + 清屏 (游戏会再设自己的配色)
        mario_run();
        serial_puts("[GAME] Mario exited\n\r");

        kbd_drain();    // 排空 ESC 释放码 (见 tetris 分支注释)

        rm_switch_mode(0x03);
        serial_puts("[GAME] Text mode restored\n\r");

        // 切回模式 03h 会重载 BIOS ROM 字体 + 25 行, 重新应用用户字号
        font_apply(font_current());

        vga_clear();
        vga_puts("MyQwen-OS v1.0.0\n\r");
    } else if (k_strcmp(argv[0], "date") == 0) {
        cmd_date();
    } else if (k_strcmp(argv[0], "font") == 0) {
        cmd_font(argc, argv);
    } else if (k_strcmp(argv[0], "pwd") == 0) {
        cmd_pwd();
    } else if (k_strcmp(argv[0], "ls") == 0) {
        cmd_ls(argc, argv);
    } else if (k_strcmp(argv[0], "cd") == 0) {
        cmd_cd(argc, argv);
    } else if (k_strcmp(argv[0], "mkdir") == 0) {
        cmd_mkdir(argc, argv);
    } else if (k_strcmp(argv[0], "touch") == 0) {
        cmd_touch(argc, argv);
    } else if (k_strcmp(argv[0], "cat") == 0) {
        cmd_cat(argc, argv);
    } else if (k_strcmp(argv[0], "rm") == 0) {
        cmd_rm(argc, argv);
    } else if (k_strcmp(argv[0], "echo") == 0) {
        cmd_echo(argc, argv);
    } else if (k_strcmp(argv[0], "df") == 0) {
        cmd_df();
    } else if (k_strcmp(argv[0], "cc") == 0) {
        if (argc < 2) {
            vga_puts("\n\rUsage: cc <source.c> [args...]\n\r");
        } else {
            // 编译并运行 C 程序 (argv[0] = 源文件, 其余传给 main)
            cc_run(argv[1], argc - 1, &argv[1]);
            kbd_drain();   // 排空运行期间/ESC 中断的残留扫描码
        }
    } else if (k_strcmp(argv[0], "splash") == 0) {
        kbd_drain();               // 触发命令的回车释放码会"跳过"动画, 先排空
        splash_run();              // 重放开机画面 (任意键跳过)
        kbd_drain();
        shell_reset_input();       // 动画期间按的其它键清掉
        vga_puts("MyQwen-OS v1.0.0\n\r");
    } else if (k_strcmp(argv[0], "edit") == 0) {
        if (argc < 2) {
            vga_puts("\n\rUsage: edit <file>\n\r");
        } else {
            // 编辑器在文本模式内直接运行 (无模式切换), 返回后恢复屏幕
            editor_run(argv[1]);
            vga_puts("MyQwen-OS v1.0.0\n\r");
        }
    } else {
        vga_puts("Unknown command: ");
        vga_puts(argv[0]);
        vga_puts("\n\r");
    }

    // 保存非空命令到历史
    if (input_index > 0) {
        if (history_count < MAX_HISTORY) {
            shell_strcpy(history[history_count], input_buffer);
            history_count++;
        } else {
            shell_strcpy(history[0], input_buffer); 
        }
    }
    
    history_pos = history_count; 
}

void shell_init() {
    shell_prompt();
}

// 清空 shell 输入缓冲 (开机画面跳过键等场景, 别把杂键带进命令)
void shell_reset_input(void) {
    input_index = 0;
    input_buffer[0] = '\0';
}

void shell_handle_char(char c) {
    if (c == '\n' || c == '\r') {
        execute_command();
        input_index = 0;
        shell_init();
    } 
    else if (c == '\b') {
        if (input_index > 0) {
            input_index--;
            vga_putc('\b');
            vga_putc(' ');
            vga_putc('\b');
        }
    }
    else if (c == '\x11') { // 上方向键
        if (history_pos > 0) {
            history_pos--;
            shell_strcpy(input_buffer, history[history_pos]);
            input_index = shell_strlen(input_buffer);
            redraw_input_line();
        }
    }
    else if (c == '\x12') { // 下方向键
        if (history_pos < history_count) {
            history_pos++;
            if (history_pos == history_count) {
                input_index = 0; 
                input_buffer[0] = '\0';
            } else {
                shell_strcpy(input_buffer, history[history_pos]);
                input_index = shell_strlen(input_buffer);
            }
            redraw_input_line();
        }
    }
    else {
        if (input_index < MAX_INPUT - 1) {
            input_buffer[input_index] = c;
            input_index++;
            vga_putc(c);
        }
    }
}
