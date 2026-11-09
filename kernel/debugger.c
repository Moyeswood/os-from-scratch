#include "debugger.h"
#include "fs.h"
#include "string.h"
#include "serial.h"
#include "vga.h"
#include "keyboard.h"
#include "user.h"
#include "types.h"

#define DBG_BASE  0x02000000u
#define DBG_MAX   16384

static uint8_t dbg_mem[DBG_MAX];
static int dbg_len;
static int dbg_modified;
static char dbg_path[FS_PATH_MAX];
static uint32_t dbg_dis_addr;

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

static void dbg_registers(void) {
    uint32_t ip = DBG_BASE + dbg_dis_addr;
    vga_puts("EAX=00000000 EBX=00000000 ECX=00000000 EDX=00000000\n\r");
    vga_puts("ESI=00000000 EDI=00000000 EBP=00000000 ESP=023FE000\n\r");
    vga_puts("EIP=");
    dbg_hex32(ip);
    vga_puts(" EFLAGS=00000202 CS=001B DS=0023 ES=0023 SS=0023\n\r");
    vga_puts("image=");
    vga_puts(dbg_path);
    vga_puts(" bytes=");
    dbg_hex32((uint32_t)dbg_len);
    vga_puts("\n\r");
    serial_puts("[DBG] r\n\r");
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

static void dbg_unassemble(uint32_t addr) {
    int off = dbg_index(addr);
    if (off < 0) off = 0;
    for (int line = 0; line < 8 && off < dbg_len; ++line) {
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
    serial_puts("[DBG] u\n\r");
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
        dbg_mem[off++] = value;
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

static void dbg_run_image(void) {
    const char* temp = "/bin/.debugrun";
    int inum = fs_lookup(temp);
    if (inum < 0) inum = fs_create(temp, FS_FILE);
    if (inum < 0 || fs_write((uint32_t)inum, dbg_mem,
                             (uint32_t)dbg_len, 0) != dbg_len) {
        vga_puts("cannot prepare run image\n\r");
        return;
    }
    serial_puts("[DBG] g\n\r");
    user_run(temp, 0, 0, 0);
    kbd_drain();
    serial_puts("[DBG] returned\n\r");
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
    dbg_modified = 0;
    dbg_dis_addr = 0;
    serial_puts("[DBG] loaded\n\r");

    vga_clear();
    vga_puts("NewOS DEBUG  type ? for help\n\r");
    vga_puts("file: ");
    vga_puts(path);
    vga_puts("\n\r");
    char line[128];
    for (;;) {
        if (dbg_readline(line, sizeof(line)) < 0) break;
        char* argv[16];
        int argc = dbg_tokenize(line, argv, 16);
        if (!argc) continue;
        uint32_t addr = DBG_BASE + dbg_dis_addr;
        if (argc > 1 && dbg_parse_u32(argv[1], &addr) < 0) {
            vga_puts("bad address\n\r");
            continue;
        }
        if (k_strcmp(argv[0], "r") == 0) {
            dbg_registers();
        } else if (k_strcmp(argv[0], "d") == 0) {
            dbg_dump(addr);
        } else if (k_strcmp(argv[0], "u") == 0) {
            dbg_unassemble(addr);
        } else if (k_strcmp(argv[0], "e") == 0) {
            dbg_edit(addr, argv, argc);
        } else if (k_strcmp(argv[0], "w") == 0) {
            dbg_write_image();
        } else if (k_strcmp(argv[0], "g") == 0) {
            dbg_run_image();
        } else if (k_strcmp(argv[0], "q") == 0) {
            break;
        } else if (k_strcmp(argv[0], "?") == 0) {
            vga_puts("r regs  d [addr] dump  u [addr] unassemble\n\r");
            vga_puts("e addr bytes  w write  g run  q quit\n\r");
        } else {
            vga_puts("unknown debug command\n\r");
        }
    }
    serial_puts("[DBG] exit\n\r");
    kbd_drain();
    vga_clear();
}
