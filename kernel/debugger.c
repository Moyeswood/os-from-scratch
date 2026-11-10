#include "debugger.h"
#include "fs.h"
#include "string.h"
#include "serial.h"
#include "vga.h"
#include "keyboard.h"
#include "user.h"
#include "proc.h"
#include "task.h"
#include "vm.h"
#include "types.h"

#define DBG_BASE       0x02000000u
#define DBG_MAX        16384
#define DBG_BREAK_MAX  8
#define DBG_TF         0x00000100u
#define DBG_RF         0x00010000u

enum {
    DBG_EVENT_NONE = 0,
    DBG_EVENT_PAUSED,
    DBG_EVENT_STEP,
    DBG_EVENT_BREAK,
    DBG_EVENT_STEP_OVER,
    DBG_EVENT_EXIT
};

typedef struct {
    int used;
    int permanent;
    int armed;
    uint32_t addr;
    uint8_t original;
} dbg_breakpoint_t;

static uint8_t dbg_mem[DBG_MAX];
static int dbg_len;
static int dbg_modified;
static char dbg_path[FS_PATH_MAX];
static uint32_t dbg_dis_addr;

static dbg_breakpoint_t dbg_breaks[DBG_BREAK_MAX];
static int dbg_target_pid;
static uint32_t* dbg_frame;
static int dbg_frame_is_exception;
static int dbg_frame_valid;
static int dbg_stop_event;
static uint32_t dbg_stop_addr;
static int dbg_rearm_index = -1;
static int dbg_hidden_resume;

static int dbg_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static int dbg_parse_u32(const char* text, uint32_t* value) {
    if (!text || !*text) return -1;
    uint32_t v = 0;
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    if (!*text) return -1;
    while (*text) {
        int d;
        char c = *text++;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        if (d >= base) return -1;
        v = v * (uint32_t)base + (uint32_t)d;
    }
    *value = v;
    return 0;
}

static int dbg_parse_byte(const char* text, uint8_t* value) {
    uint32_t v;
    if (dbg_parse_u32(text, &v) < 0 || v > 255) return -1;
    *value = (uint8_t)v;
    return 0;
}

static int dbg_index(uint32_t addr) {
    if (addr < DBG_BASE) return -1;
    uint32_t off = addr - DBG_BASE;
    return off < (uint32_t)dbg_len ? (int)off : -1;
}

static void dbg_hex8(uint8_t value) {
    const char* hex = "0123456789ABCDEF";
    vga_putc(hex[(value >> 4) & 0xF]);
    vga_putc(hex[value & 0xF]);
}

static void dbg_hex32(uint32_t value) {
    const char* hex = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4)
        vga_putc(hex[(value >> shift) & 0xF]);
}

static int dbg_readline(char* out, int cap) {
    int len = 0;
    vga_puts("- ");
    for (;;) {
        int c = kbd_getchar_blocking(0);
        if (c < 0) continue;
        if (c == 27) return -1;
        if (c == '\n' || c == '\r') {
            out[len] = '\0';
            vga_puts("\n\r");
            return 1;
        }
        if (c == '\b') {
            if (len) {
                --len;
                vga_puts("\b \b");
            }
            continue;
        }
        if (c >= 32 && len < cap - 1) {
            out[len++] = (char)c;
            vga_putc((char)c);
        }
    }
}

static int dbg_tokenize(char* line, char** argv, int max) {
    int argc = 0;
    char* p = line;
    while (*p && argc < max) {
        while (dbg_space(*p)) ++p;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && !dbg_space(*p)) ++p;
        if (*p) *p++ = '\0';
    }
    return argc;
}

static task_t* dbg_target(void) {
    return dbg_target_pid > 0 ? task_by_pid(dbg_target_pid) : 0;
}

static uint32_t dbg_frame_eip(void) {
    if (!dbg_frame) return DBG_BASE + dbg_dis_addr;
    return dbg_frame[dbg_frame_is_exception ? 10 : 8];
}

static uint32_t dbg_frame_eflags(void) {
    if (!dbg_frame) return 0;
    return dbg_frame[dbg_frame_is_exception ? 12 : 10];
}

static uint32_t dbg_frame_esp(void) {
    if (!dbg_frame) return 0;
    return dbg_frame[dbg_frame_is_exception ? 13 : 11];
}

static void dbg_set_frame_eip(uint32_t eip) {
    if (dbg_frame) dbg_frame[dbg_frame_is_exception ? 10 : 8] = eip;
}

static void dbg_set_frame_eflags(uint32_t eflags) {
    if (dbg_frame) dbg_frame[dbg_frame_is_exception ? 12 : 10] = eflags;
}

static void dbg_serial_frame(const char* tag) {
    serial_puts("[DBG] ");
    serial_puts(tag);
    serial_puts(" eip=");
    serial_put_hex(dbg_frame_eip());
    serial_puts(" eax=");
    serial_put_hex(dbg_frame ? dbg_frame[7] : 0);
    serial_puts(" ebx=");
    serial_put_hex(dbg_frame ? dbg_frame[4] : 0);
    serial_puts(" ecx=");
    serial_put_hex(dbg_frame ? dbg_frame[6] : 0);
    serial_puts(" edx=");
    serial_put_hex(dbg_frame ? dbg_frame[5] : 0);
    serial_puts(" esp=");
    serial_put_hex(dbg_frame_esp());
    serial_puts(" eflags=");
    serial_put_hex(dbg_frame_eflags());
    serial_puts("\n\r");
}

static void dbg_registers(int log) {
    task_t* t = dbg_target();
    if (!dbg_frame_valid || !t || !dbg_frame) {
        vga_puts("no live register context (target exited)\n\r");
        if (log) serial_puts("[DBG] r exited\n\r");
        return;
    }

    vga_puts("EAX=");
    dbg_hex32(dbg_frame[7]);
    vga_puts(" EBX=");
    dbg_hex32(dbg_frame[4]);
    vga_puts(" ECX=");
    dbg_hex32(dbg_frame[6]);
    vga_puts(" EDX=");
    dbg_hex32(dbg_frame[5]);
    vga_puts("\n\r");
    vga_puts("ESI=");
    dbg_hex32(dbg_frame[1]);
    vga_puts(" EDI=");
    dbg_hex32(dbg_frame[0]);
    vga_puts(" EBP=");
    dbg_hex32(dbg_frame[2]);
    vga_puts(" ESP=");
    dbg_hex32(dbg_frame_esp());
    vga_puts("\n\r");
    vga_puts("EIP=");
    dbg_hex32(dbg_frame_eip());
    vga_puts(" EFLAGS=");
    dbg_hex32(dbg_frame_eflags());
    vga_puts(" CS=");
    dbg_hex32(dbg_frame[dbg_frame_is_exception ? 11 : 9]);
    vga_puts(" SS=0023\n\r");

    uint32_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    vga_puts("CR2=");
    dbg_hex32(cr2);
    vga_puts(" CR3=");
    dbg_hex32(t->cr3);
    vga_puts(" pid=");
    dbg_hex32(t->pid);
    vga_puts("\n\r");
    vga_puts("image=");
    vga_puts(dbg_path);
    vga_puts(" bytes=");
    dbg_hex32((uint32_t)dbg_len);
    vga_puts("\n\r");

    if (log) {
        serial_puts("[DBG] r\n\r");
        dbg_serial_frame("regs");
    }
}

static void dbg_dump(uint32_t addr) {
    int start = dbg_index(addr);
    if (start < 0) {
        start = 0;
        addr = DBG_BASE;
    }
    for (int row = 0; row < 8; ++row) {
        int off = start + row * 16;
        if (off >= dbg_len) break;
        dbg_hex32(addr + (uint32_t)(row * 16));
        vga_puts(": ");
        for (int col = 0; col < 16; ++col) {
            if (off + col < dbg_len) dbg_hex8(dbg_mem[off + col]);
            else vga_puts("  ");
            vga_putc(' ');
        }
        vga_puts(" |");
        for (int col = 0; col < 16 && off + col < dbg_len; ++col) {
            char c = (char)dbg_mem[off + col];
            vga_putc(c >= 32 && c < 127 ? c : '.');
        }
        vga_puts("|\n\r");
    }
    serial_puts("[DBG] d\n\r");
}

static int dbg_modrm_len(uint8_t modrm, int remaining) {
    int len = 1;
    int mod = modrm >> 6;
    int rm = modrm & 7;
    if (mod == 3) return len;
    if (rm == 4) {
        if (remaining < 2) return remaining;
        ++len;
    }
    if (mod == 0 && rm == 5) len += 4;
    else if (mod == 1) ++len;
    else if (mod == 2) len += 4;
    return len;
}

static int dbg_insn_len(const uint8_t* p, int remaining) {
    if (!remaining) return 0;
    uint8_t op = p[0];
    if (op >= 0xB8 && op <= 0xBF) return remaining >= 5 ? 5 : remaining;
    if (op == 0x0F) return remaining >= 6 ? 6 : remaining;
    if (op == 0x81 || op == 0xC7 || op == 0x68) return remaining >= 6 ? 6 : remaining;
    if (op == 0xC1) return remaining >= 3 ? 3 : remaining;
    if (op == 0xCD) return remaining >= 2 ? 2 : remaining;
    if (op == 0xE8 || op == 0xE9) return remaining >= 5 ? 5 : remaining;
    if (op == 0x89 || op == 0x8B || op == 0x8D ||
        op == 0x31 || op == 0x09 || op == 0x21 || op == 0x29 ||
        op == 0x39 || op == 0x85 || op == 0xF7) {
        if (remaining < 2) return remaining;
        return 1 + dbg_modrm_len(p[1], remaining - 1);
    }
    if (op >= 0x40 && op <= 0x5F) return 1;
    return 1;
}

static const char* dbg_reg_name(int reg) {
    static const char* names[] = {
        "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"
    };
    return names[reg & 7];
}

static void dbg_unassemble_count(uint32_t addr, int count, int log) {
    int off = dbg_index(addr);
    if (off < 0) off = 0;
    for (int line = 0; line < count && off < dbg_len; ++line) {
        uint8_t* p = dbg_mem + off;
        int left = dbg_len - off;
        int len = dbg_insn_len(p, left);
        uint32_t current = DBG_BASE + (uint32_t)off;
        dbg_hex32(current);
        vga_puts(": ");
        for (int i = 0; i < len && i < 6; ++i) {
            dbg_hex8(p[i]);
            vga_putc(' ');
        }
        for (int i = len; i < 6; ++i) vga_puts("   ");
        uint8_t op = p[0];
        if (op >= 0xB8 && op <= 0xBF && len == 5) {
            uint32_t imm = (uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                           ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
            vga_puts("mov ");
            vga_puts(dbg_reg_name(op - 0xB8));
            vga_puts(",0x");
            dbg_hex32(imm);
        } else if (op == 0xCD) {
            vga_puts("int 0x");
            dbg_hex32(p[1]);
        } else if (op == 0x31 && len >= 2 && (p[1] >> 6) == 3) {
            vga_puts("xor ");
            vga_puts(dbg_reg_name(p[1] & 7));
            vga_puts(",");
            vga_puts(dbg_reg_name((p[1] >> 3) & 7));
        } else if (op == 0xE8 || op == 0xE9) {
            int rel = (int)((uint32_t)p[1] |
                       ((uint32_t)p[2] << 8) |
                       ((uint32_t)p[3] << 16) |
                       ((uint32_t)p[4] << 24));
            vga_puts(op == 0xE8 ? "call " : "jmp ");
            dbg_hex32(current + (uint32_t)len + (uint32_t)rel);
        } else if (op == 0x0F && len >= 6 && p[1] >= 0x80 && p[1] <= 0x8F) {
            int rel = (int)((uint32_t)p[2] |
                       ((uint32_t)p[3] << 8) |
                       ((uint32_t)p[4] << 16) |
                       ((uint32_t)p[5] << 24));
            vga_puts("jcc ");
            dbg_hex32(current + (uint32_t)len + (uint32_t)rel);
        } else if (op == 0xC3) vga_puts("ret");
        else if (op == 0x90) vga_puts("nop");
        else if (op == 0xF4) vga_puts("hlt");
        else {
            vga_puts("db 0x");
            dbg_hex8(op);
        }
        vga_puts("\n\r");
        off += len ? len : 1;
        dbg_dis_addr = (uint32_t)off;
    }
    if (log) serial_puts("[DBG] u\n\r");
}

static void dbg_stack(void) {
    task_t* t = dbg_target();
    if (!t || !dbg_frame_valid) return;
    uint32_t words[4];
    uint32_t esp = dbg_frame_esp();
    if (vm_read_user(t, esp, words, sizeof(words)) < 0) {
        vga_puts("stack: unavailable\n\r");
        return;
    }
    vga_puts("stack ");
    dbg_hex32(esp);
    vga_puts(": ");
    for (int i = 0; i < 4; ++i) {
        dbg_hex32(words[i]);
        vga_putc(' ');
    }
    vga_puts("\n\r");
    serial_puts("[DBG] stack esp=");
    serial_put_hex(esp);
    serial_puts(" top=");
    serial_put_hex(words[0]);
    serial_puts("\n\r");
}

static int dbg_break_find(uint32_t addr) {
    for (int i = 0; i < DBG_BREAK_MAX; ++i) {
        if (dbg_breaks[i].used && dbg_breaks[i].addr == addr) return i;
    }
    return -1;
}

static int dbg_target_write_byte(uint32_t addr, uint8_t value) {
    task_t* t = dbg_target();
    if (!t || t->state == TASK_ZOMBIE || t->state == TASK_IDLE) return -1;
    return vm_write_user(t, addr, &value, 1);
}

static int dbg_install_breakpoint(uint32_t addr, int permanent) {
    int off = dbg_index(addr);
    if (off < 0) return -1;
    int old = dbg_break_find(addr);
    if (old >= 0) {
        if (permanent) dbg_breaks[old].permanent = 1;
        return old;
    }

    int slot = -1;
    for (int i = 0; i < DBG_BREAK_MAX; ++i) {
        if (!dbg_breaks[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    dbg_breaks[slot].used = 1;
    dbg_breaks[slot].permanent = permanent;
    dbg_breaks[slot].addr = addr;
    dbg_breaks[slot].original = dbg_mem[off];
    if (dbg_target_write_byte(addr, 0xCC) < 0) {
        dbg_breaks[slot].used = 0;
        return -1;
    }
    uint8_t installed = 0;
    task_t* target = dbg_target();
    vm_read_user(target, addr, &installed, 1);
    serial_puts("[DBG] breakpoint armed addr=");
    serial_put_hex(addr);
    serial_puts(" byte=");
    serial_put_hex(installed);
    serial_puts("\n\r");
    dbg_breaks[slot].armed = 1;
    return slot;
}

static void dbg_rearm_breakpoint(int index) {
    if (index < 0 || index >= DBG_BREAK_MAX || !dbg_breaks[index].used)
        return;
    if (dbg_target_write_byte(dbg_breaks[index].addr, 0xCC) == 0)
        dbg_breaks[index].armed = 1;
}

static void dbg_restore_breakpoint(int index) {
    if (index < 0 || index >= DBG_BREAK_MAX || !dbg_breaks[index].used)
        return;
    dbg_target_write_byte(dbg_breaks[index].addr,
                          dbg_breaks[index].original);
    dbg_breaks[index].armed = 0;
}

static void dbg_clear_breakpoints(void) {
    for (int i = 0; i < DBG_BREAK_MAX; ++i) {
        if (dbg_breaks[i].used && dbg_breaks[i].armed)
            dbg_restore_breakpoint(i);
        k_memset(&dbg_breaks[i], 0, sizeof(dbg_breaks[i]));
    }
    dbg_rearm_index = -1;
    serial_puts("[DBG] breakpoints cleared\n\r");
}

static void dbg_list_breakpoints(void) {
    int any = 0;
    vga_puts("breakpoints:");
    for (int i = 0; i < DBG_BREAK_MAX; ++i) {
        if (!dbg_breaks[i].used) continue;
        any = 1;
        vga_puts(" #");
        dbg_hex8((uint8_t)i);
        vga_puts("=");
        dbg_hex32(dbg_breaks[i].addr);
        if (!dbg_breaks[i].permanent) vga_puts(" temp");
        if (!dbg_breaks[i].armed) vga_puts(" pending");
    }
    if (!any) vga_puts(" none");
    vga_puts("\n\r");
    serial_puts("[DBG] bp list\n\r");
}

static void dbg_stop_current(int event, uint32_t addr) {
    dbg_stop_event = event;
    dbg_stop_addr = addr;
    serial_puts("[DBG] target stopped event=");
    serial_put_hex((uint32_t)event);
    serial_puts(" addr=");
    serial_put_hex(addr);
    serial_puts("\n\r");
    task_stop_switch_away();
}

static void dbg_handle_breakpoint(uint32_t* frame) {
    uint32_t after = frame[10];
    uint32_t addr = after - 1;
    int index = dbg_break_find(addr);

    if (index >= 0) {
        uint8_t original = dbg_breaks[index].original;
        dbg_target_write_byte(addr, original);
        uint8_t restored = 0;
        task_t* target = dbg_target();
        vm_read_user(target, addr, &restored, 1);
        serial_puts("[DBG] breakpoint restored addr=");
        serial_put_hex(addr);
        serial_puts(" byte=");
        serial_put_hex(restored);
        serial_puts("\n\r");
        dbg_breaks[index].armed = 0;
        frame[10] = addr;
        if (dbg_breaks[index].permanent) {
            dbg_rearm_index = index;
            dbg_stop_current(DBG_EVENT_BREAK, addr);
        } else {
            dbg_breaks[index].used = 0;
            dbg_stop_current(DBG_EVENT_STEP_OVER, addr);
        }
        return;
    }

    int off = dbg_index(addr);
    if (off >= 0) dbg_target_write_byte(addr, dbg_mem[off]);
    frame[10] = addr;
    dbg_stop_current(DBG_EVENT_BREAK, addr);
}

int debugger_exception(uint32_t* frame) {
    task_t* t = task_current();
    if (!frame || dbg_target_pid <= 0 || !t ||
        (int)t->pid != dbg_target_pid || !t->is_user)
        return 0;
    uint32_t vec = frame[8];
    if (vec != 1 && vec != 3) return 0;

    dbg_frame = frame;
    dbg_frame_is_exception = 1;
    dbg_frame_valid = 1;

    if (dbg_rearm_index >= 0) {
        dbg_rearm_breakpoint(dbg_rearm_index);
        dbg_rearm_index = -1;
    }

    // TF 只控制下一条指令；进入处理器后必须清掉，避免连续 #DB。
    frame[12] &= ~DBG_TF;
    if (vec == 3) {
        dbg_handle_breakpoint(frame);
        return 1;
    }

    if (dbg_hidden_resume) {
        dbg_hidden_resume = 0;
        return 1;
    }
    dbg_stop_current(DBG_EVENT_STEP, frame[10]);
    return 1;
}

static void dbg_show_stop(void) {
    const char* reason = "paused";
    if (dbg_stop_event == DBG_EVENT_STEP) reason = "single-step complete";
    else if (dbg_stop_event == DBG_EVENT_BREAK) reason = "breakpoint hit";
    else if (dbg_stop_event == DBG_EVENT_STEP_OVER) reason = "step-over complete";
    else if (dbg_stop_event == DBG_EVENT_EXIT) reason = "target exited";

    vga_puts("\n\r[");
    vga_puts(reason);
    if (dbg_stop_event != DBG_EVENT_EXIT) {
        vga_puts(" @ ");
        dbg_hex32(dbg_stop_addr);
    }
    vga_puts("]\n\r");
    serial_puts("[DBG] stop reason=");
    serial_puts(reason);
    serial_puts(" eip=");
    serial_put_hex(dbg_frame_valid ? dbg_frame_eip() : dbg_stop_addr);
    serial_puts("\n\r");

    if (dbg_stop_event == DBG_EVENT_EXIT) {
        task_t* t = dbg_target();
        if (t) {
            vga_puts("exit code=");
            dbg_hex32((uint32_t)t->exit_code);
            vga_puts("\n\r");
        }
        serial_puts("[DBG] returned\n\r");
        return;
    }

    dbg_registers(0);
    dbg_unassemble_count(dbg_frame_eip(), 3, 0);
    dbg_stack();
}

static void dbg_wait_for_target(void) {
    for (;;) {
        __asm__ volatile("cli");
        task_t* t = dbg_target();
        if (!t || t->state == TASK_STOPPED || t->state == TASK_ZOMBIE) {
            __asm__ volatile("sti");
            break;
        }
        task_sleep_switch_locked(TASK_WAIT_PROC, 0);
        __asm__ volatile("sti");
    }

    task_t* t = dbg_target();
    if (!t || t->state == TASK_ZOMBIE) {
        dbg_frame_valid = 0;
        dbg_stop_event = DBG_EVENT_EXIT;
        dbg_stop_addr = 0;
    }
}

static void dbg_resume_target(int single_step, int hidden) {
    task_t* t = dbg_target();
    if (!t || !dbg_frame || t->state != TASK_STOPPED) return;
    uint32_t eflags = dbg_frame_eflags();
    eflags &= ~DBG_RF;
    if (single_step) eflags |= DBG_TF;
    else eflags &= ~DBG_TF;
    dbg_set_frame_eflags(eflags);
    dbg_hidden_resume = hidden;
    dbg_stop_event = DBG_EVENT_NONE;

    serial_puts("[DBG] resume");
    if (single_step) serial_puts(" tf");
    if (hidden) serial_puts(" hidden");
    serial_puts("\n\r");

    __asm__ volatile("cli");
    t->state = TASK_READY;
    t->wait_flags = 0;
    t->sleep_until = 0;
    task_sleep_switch_locked(TASK_WAIT_PROC, 0);
    __asm__ volatile("sti");
    dbg_wait_for_target();
    dbg_show_stop();
}

static void dbg_continue(void) {
    int step_original = dbg_rearm_index >= 0;
    serial_puts("[DBG] g\n\r");
    dbg_resume_target(step_original, step_original);
}

static void dbg_single_step(void) {
    serial_puts("[DBG] t\n\r");
    dbg_resume_target(1, 0);
}

static void dbg_step_over(void) {
    task_t* t = dbg_target();
    uint8_t bytes[5];
    uint32_t eip = dbg_frame_eip();
    if (!t || !dbg_frame_valid ||
        vm_read_user(t, eip, bytes, sizeof(bytes)) < 0 ||
        bytes[0] != 0xE8 || dbg_index(eip + 5) < 0) {
        dbg_single_step();
        return;
    }

    int index = dbg_install_breakpoint(eip + 5, 0);
    if (index < 0) {
        dbg_single_step();
        return;
    }
    serial_puts("[DBG] step-over return=");
    serial_put_hex(eip + 5);
    serial_puts("\n\r");
    serial_puts("[DBG] p\n\r");
    dbg_resume_target(0, 0);
}

static void dbg_edit(uint32_t addr, char** argv, int argc) {
    int off = dbg_index(addr);
    if (off < 0) {
        vga_puts("address outside image\n\r");
        return;
    }
    for (int i = 2; i < argc && off < dbg_len; ++i) {
        uint8_t value;
        if (dbg_parse_byte(argv[i], &value) < 0) break;
        dbg_mem[off] = value;
        int break_index = dbg_break_find(DBG_BASE + (uint32_t)off);
        if (break_index >= 0 && dbg_breaks[break_index].armed)
            value = 0xCC;
        dbg_target_write_byte(DBG_BASE + (uint32_t)off, value);
        ++off;
        dbg_modified = 1;
    }
    vga_puts("image bytes modified\n\r");
    serial_puts("[DBG] e\n\r");
}

static int dbg_write_image(void) {
    int inum = fs_lookup(dbg_path);
    if (inum < 0) inum = fs_create(dbg_path, FS_FILE);
    if (inum < 0 || fs_write((uint32_t)inum, dbg_mem,
                             (uint32_t)dbg_len, 0) != dbg_len) {
        vga_puts("write failed\n\r");
        serial_puts("[DBG] w fail\n\r");
        return -1;
    }
    dbg_modified = 0;
    vga_puts("image written\n\r");
    serial_puts("[DBG] w\n\r");
    return 0;
}

static void dbg_cleanup_target(void) {
    task_t* t = dbg_target();
    if (t && t->state != TASK_ZOMBIE) proc_kill((int)t->pid);
    if (dbg_target_pid > 0 && task_by_pid(dbg_target_pid))
        proc_wait(dbg_target_pid);
    dbg_target_pid = 0;
    dbg_frame = 0;
    dbg_frame_valid = 0;
}

static void dbg_help(void) {
    vga_puts("r regs  t step  p step-over  g continue  bp [addr]\n\r");
    vga_puts("bl list  bc clear  d [addr]  u [addr]  e addr bytes\n\r");
    vga_puts("w write image  q quit\n\r");
}

void debug_run(const char* path) {
    k_strcpy(dbg_path, path);
    int inum = fs_lookup(path);
    if (inum < 0 || fs_inode_type((uint32_t)inum) != FS_FILE) {
        serial_puts("[DBG] load fail\n\r");
        vga_puts("debug: cannot load file\n\r");
        return;
    }
    dbg_len = 0;
    while (dbg_len < DBG_MAX) {
        int n = fs_read((uint32_t)inum, dbg_mem + dbg_len,
                        (uint32_t)dbg_len, (uint32_t)(DBG_MAX - dbg_len));
        if (n <= 0) break;
        dbg_len += n;
    }
    if (dbg_len == 0) {
        serial_puts("[DBG] load fail empty\n\r");
        vga_puts("debug: empty image\n\r");
        return;
    }

    dbg_modified = 0;
    dbg_dis_addr = 0;
    k_memset(dbg_breaks, 0, sizeof(dbg_breaks));
    dbg_rearm_index = -1;
    dbg_hidden_resume = 0;
    serial_puts("[DBG] loaded\n\r");

    int pid = proc_spawn_file_paused(path);
    if (pid < 0) {
        serial_puts("[DBG] spawn fail\n\r");
        vga_puts("debug: cannot create paused process\n\r");
        return;
    }
    dbg_target_pid = pid;
    task_t* t = dbg_target();
    dbg_frame = t ? (uint32_t*)t->esp : 0;
    dbg_frame_is_exception = 0;
    dbg_frame_valid = t != 0;
    dbg_stop_event = DBG_EVENT_PAUSED;
    dbg_stop_addr = dbg_frame_valid ? dbg_frame_eip() : 0;
    serial_puts("[DBG] target paused pid=");
    serial_put_hex((uint32_t)pid);
    serial_puts(" eip=");
    serial_put_hex(dbg_stop_addr);
    serial_puts("\n\r");

    vga_clear();
    vga_puts("NewOS DEBUG  type ? for help\n\r");
    vga_puts("file: ");
    vga_puts(path);
    vga_puts("\n\r");
    dbg_show_stop();

    char line[128];
    for (;;) {
        if (dbg_readline(line, sizeof(line)) < 0) break;
        char* argv[16];
        int argc = dbg_tokenize(line, argv, 16);
        if (!argc) continue;
        uint32_t addr = dbg_frame_valid ? dbg_frame_eip() :
                        DBG_BASE + dbg_dis_addr;
        if (argc > 1 && dbg_parse_u32(argv[1], &addr) < 0) {
            vga_puts("bad address\n\r");
            continue;
        }

        if (k_strcmp(argv[0], "r") == 0) {
            dbg_registers(1);
        } else if (k_strcmp(argv[0], "d") == 0) {
            if (argc == 1) addr = DBG_BASE + dbg_dis_addr;
            dbg_dump(addr);
        } else if (k_strcmp(argv[0], "u") == 0) {
            dbg_unassemble_count(addr, 8, 1);
        } else if (k_strcmp(argv[0], "e") == 0) {
            dbg_edit(addr, argv, argc);
        } else if (k_strcmp(argv[0], "w") == 0) {
            dbg_write_image();
        } else if (k_strcmp(argv[0], "g") == 0) {
            if (argc > 1) dbg_set_frame_eip(addr);
            dbg_continue();
        } else if (k_strcmp(argv[0], "t") == 0) {
            if (argc > 1) dbg_set_frame_eip(addr);
            dbg_single_step();
        } else if (k_strcmp(argv[0], "p") == 0) {
            dbg_step_over();
        } else if (k_strcmp(argv[0], "bp") == 0 ||
                   k_strcmp(argv[0], "b") == 0) {
            if (dbg_install_breakpoint(addr, 1) >= 0) {
                vga_puts("breakpoint set at ");
                dbg_hex32(addr);
                vga_puts("\n\r");
                serial_puts("[DBG] bp set\n\r");
            } else {
                vga_puts("cannot set breakpoint\n\r");
            }
        } else if (k_strcmp(argv[0], "bl") == 0) {
            dbg_list_breakpoints();
        } else if (k_strcmp(argv[0], "bc") == 0) {
            dbg_clear_breakpoints();
        } else if (k_strcmp(argv[0], "q") == 0) {
            break;
        } else if (k_strcmp(argv[0], "?") == 0) {
            dbg_help();
        } else {
            vga_puts("unknown debug command\n\r");
        }
    }

    dbg_cleanup_target();
    serial_puts("[DBG] exit\n\r");
    kbd_drain();
    vga_clear();
}
