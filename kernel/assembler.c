#include "assembler.h"
#include "fs.h"
#include "string.h"
#include "serial.h"
#include "types.h"

#define ASM_ORIGIN   0x02000000u
#define ASM_SRC_MAX  16384
#define ASM_OUT_MAX  16384
#define ASM_LABELS   128
#define ASM_NAME     40

typedef struct {
    char name[ASM_NAME];
    uint32_t value;
} asm_label_t;

enum {
    OP_NONE = 0,
    OP_REG,
    OP_IMM,
    OP_MEM,
};

typedef struct {
    int type;
    int reg;
    int base;
    int has_base;
    uint32_t value;
} asm_operand_t;

static char asm_src[ASM_SRC_MAX];
static char asm_out[ASM_OUT_MAX];
static asm_label_t asm_labels[ASM_LABELS];
static int asm_label_count;
static int asm_pass;
static int asm_out_len;
static int asm_line_no;
static char asm_error[96];

static int asm_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static char* asm_trim(char* s) {
    while (asm_is_space(*s)) ++s;
    char* end = s + k_strlen(s);
    while (end > s && asm_is_space(end[-1])) --end;
    *end = '\0';
    return s;
}

static int asm_lower(char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int asm_equal(const char* a, const char* b) {
    while (*a && *b) {
        if (asm_lower(*a) != asm_lower(*b)) return 0;
        ++a;
        ++b;
    }
    return !*a && !*b;
}

static int asm_prefix(const char* text, const char* prefix) {
    while (*prefix) {
        if (asm_lower(*text++) != asm_lower(*prefix++)) return 0;
    }
    return 1;
}

static int asm_fail(const char* message) {
    k_strcpy(asm_error, message);
    return -1;
}

static void asm_emit8(uint32_t value, int emit) {
    if (emit && asm_out_len < ASM_OUT_MAX)
        asm_out[asm_out_len] = (char)(value & 0xFF);
    ++asm_out_len;
}

static void asm_emit32(uint32_t value, int emit) {
    asm_emit8(value, emit);
    asm_emit8(value >> 8, emit);
    asm_emit8(value >> 16, emit);
    asm_emit8(value >> 24, emit);
}

static int asm_reg(const char* name) {
    static const char* names[] = {
        "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", 0
    };
    for (int i = 0; names[i]; ++i)
        if (asm_equal(name, names[i])) return i;
    return -1;
}

static int asm_parse_number(const char* text, uint32_t* value) {
    if (!text || !*text) return -1;
    uint32_t v = 0;
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    if (!*text) return -1;
    while (*text) {
        int digit;
        char c = *text++;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return -1;
        if (digit >= base) return -1;
        v = v * (uint32_t)base + (uint32_t)digit;
    }
    *value = v;
    return 0;
}

static int asm_find_label(const char* name) {
    for (int i = 0; i < asm_label_count; ++i)
        if (asm_equal(asm_labels[i].name, name)) return i;
    return -1;
}

static int asm_define_label(const char* name) {
    if (k_strlen(name) >= ASM_NAME) return asm_fail("label too long");
    if (asm_find_label(name) >= 0) return asm_fail("duplicate label");
    if (asm_label_count >= ASM_LABELS) return asm_fail("too many labels");
    k_strcpy(asm_labels[asm_label_count].name, name);
    asm_labels[asm_label_count].value = ASM_ORIGIN + (uint32_t)asm_out_len;
    ++asm_label_count;
    return 0;
}

static int asm_expr(const char* text, uint32_t* value) {
    text = asm_trim((char*)text);
    int neg = 0;
    if (*text == '+') ++text;
    else if (*text == '-') { neg = 1; ++text; }
    if (!*text) return -1;
    uint32_t result;
    if (asm_parse_number(text, &result) < 0) {
        int i = asm_find_label(text);
        if (i < 0) {
            if (asm_pass == 1) result = 0;
            else return asm_fail("unknown label");
        } else result = asm_labels[i].value;
    }
    *value = neg ? (uint32_t)(-(int)result) : result;
    return 0;
}

static int asm_parse_operand(const char* text, asm_operand_t* op) {
    k_memset(op, 0, sizeof(*op));
    char temp[96];
    if (k_strlen(text) >= sizeof(temp)) return asm_fail("operand too long");
    k_strcpy(temp, text);
    char* s = asm_trim(temp);
    if (!*s) return -1;

    int reg = asm_reg(s);
    if (reg >= 0) {
        op->type = OP_REG;
        op->reg = reg;
        return 0;
    }
    if (*s == '[') {
        char* end = s + k_strlen(s) - 1;
        if (end <= s || *end != ']') return asm_fail("missing ]");
        *end = '\0';
        s = asm_trim(s + 1);
        if (!*s) return asm_fail("empty memory operand");
        reg = asm_reg(s);
        if (reg >= 0) {
            op->has_base = 1;
            op->base = reg;
            s += 3;
            s = asm_trim(s);
            if (*s == '+' || *s == '-') {
                int neg = *s++ == '-';
                uint32_t disp;
                if (asm_expr(s, &disp) < 0) return -1;
                op->value = neg ? (uint32_t)(-(int)disp) : disp;
            }
        } else {
            if (asm_expr(s, &op->value) < 0) return -1;
        }
        op->type = OP_MEM;
        return 0;
    }

    if (asm_expr(s, &op->value) < 0) return -1;
    op->type = OP_IMM;
    return 0;
}

static int asm_split_operands(char* line, char* first, char* second) {
    first[0] = second[0] = '\0';
    char* comma = 0;
    int depth = 0;
    char quote = 0;
    for (char* p = line; *p; ++p) {
        if (*p == '"' && quote != '\'') quote = quote ? 0 : '"';
        else if (*p == '\'' && quote != '"') quote = quote ? 0 : '\'';
        else if (!quote && *p == '[') ++depth;
        else if (!quote && *p == ']') --depth;
        else if (!quote && !depth && *p == ',') { comma = p; break; }
    }
    if (!comma) {
        k_strcpy(first, line);
        return 1;
    }
    *comma = '\0';
    k_strcpy(first, line);
    k_strcpy(second, comma + 1);
    return 2;
}

static int asm_emit_rm(asm_operand_t* rm, int reg_field, int emit) {
    int modrm = (reg_field & 7) << 3;
    if (rm->type == OP_REG) {
        asm_emit8((uint32_t)(0xC0 | modrm | (rm->reg & 7)), emit);
        return 0;
    }
    if (!rm->has_base) {
        asm_emit8((uint32_t)(modrm | 5), emit);
        asm_emit32(rm->value, emit);
        return 0;
    }
    asm_emit8((uint32_t)(0x80 | modrm | (rm->base & 7)), emit);
    if ((rm->base & 7) == 4) asm_emit8(0x24, emit);
    asm_emit32(rm->value, emit);
    return 0;
}

static int asm_emit_direct_reg(int opcode, int reg, int emit) {
    asm_emit8((uint32_t)opcode + (uint32_t)(reg & 7), emit);
    return 0;
}

static int asm_branch_opcode(const char* name, int* opcode) {
    static const char* names[] = {
        "je", "jne", "jl", "jle", "jg", "jge",
        "jb", "jbe", "ja", "jae", 0
    };
    static const int codes[] = {
        0x84, 0x85, 0x8C, 0x8E, 0x8F, 0x8D,
        0x82, 0x86, 0x87, 0x83
    };
    for (int i = 0; names[i]; ++i) {
        if (asm_equal(name, names[i])) {
            *opcode = codes[i];
            return 1;
        }
    }
    return 0;
}

static int asm_encode_instruction(char* text, int emit) {
    char* mnemonic = text;
    char* rest = mnemonic;
    while (*rest && !asm_is_space(*rest)) ++rest;
    if (*rest) *rest++ = '\0';
    rest = asm_trim(rest);

    char left[96], right[96];
    int argc = asm_split_operands(rest, left, right);
    asm_operand_t a, b;
    k_memset(&a, 0, sizeof(a));
    k_memset(&b, 0, sizeof(b));
    if (argc >= 1 && *asm_trim(left) && asm_parse_operand(left, &a) < 0) return -1;
    if (argc >= 2 && *asm_trim(right) && asm_parse_operand(right, &b) < 0) return -1;

    if (asm_equal(mnemonic, "nop")) { asm_emit8(0x90, emit); return 0; }
    if (asm_equal(mnemonic, "ret")) { asm_emit8(0xC3, emit); return 0; }
    if (asm_equal(mnemonic, "hlt")) { asm_emit8(0xF4, emit); return 0; }
    if (asm_equal(mnemonic, "syscall")) {
        asm_emit8(0xCD, emit); asm_emit8(0x80, emit); return 0;
    }
    if (asm_equal(mnemonic, "int")) {
        if (a.type != OP_IMM) return asm_fail("int needs immediate");
        asm_emit8(0xCD, emit); asm_emit8(a.value, emit); return 0;
    }
    if (asm_equal(mnemonic, "push")) {
        if (a.type == OP_REG) { asm_emit_direct_reg(0x50, a.reg, emit); return 0; }
        if (a.type == OP_IMM) { asm_emit8(0x68, emit); asm_emit32(a.value, emit); return 0; }
        return asm_fail("unsupported push");
    }
    if (asm_equal(mnemonic, "pop")) {
        if (a.type != OP_REG) return asm_fail("pop needs register");
        asm_emit_direct_reg(0x58, a.reg, emit); return 0;
    }
    if (asm_equal(mnemonic, "inc") || asm_equal(mnemonic, "dec")) {
        if (a.type != OP_REG) return asm_fail("unsupported inc/dec");
        asm_emit_direct_reg(asm_equal(mnemonic, "inc") ? 0x40 : 0x48,
                            a.reg, emit);
        return 0;
    }
    if (asm_equal(mnemonic, "mul") || asm_equal(mnemonic, "div") ||
        asm_equal(mnemonic, "not") || asm_equal(mnemonic, "neg")) {
        int digit = asm_equal(mnemonic, "mul") ? 4 :
                    asm_equal(mnemonic, "div") ? 6 :
                    asm_equal(mnemonic, "not") ? 2 : 3;
        asm_emit8(0xF7, emit);
        return asm_emit_rm(&a, digit, emit);
    }
    if (asm_equal(mnemonic, "lea")) {
        if (a.type != OP_REG || b.type != OP_MEM)
            return asm_fail("lea needs reg,mem");
        asm_emit8(0x8D, emit);
        return asm_emit_rm(&b, a.reg, emit);
    }
    if (asm_equal(mnemonic, "mov")) {
        if (a.type == OP_REG && b.type == OP_IMM) {
            asm_emit_direct_reg(0xB8, a.reg, emit);
            asm_emit32(b.value, emit);
            return 0;
        }
        if (a.type == OP_REG && b.type == OP_REG) {
            asm_emit8(0x89, emit);
            asm_emit8((uint32_t)(0xC0 | (b.reg << 3) | a.reg), emit);
            return 0;
        }
        if (a.type == OP_REG && b.type == OP_MEM) {
            asm_emit8(0x8B, emit);
            return asm_emit_rm(&b, a.reg, emit);
        }
        if (a.type == OP_MEM && b.type == OP_REG) {
            asm_emit8(0x89, emit);
            return asm_emit_rm(&a, b.reg, emit);
        }
        if (a.type == OP_MEM && b.type == OP_IMM) {
            asm_emit8(0xC7, emit);
            if (asm_emit_rm(&a, 0, emit) < 0) return -1;
            asm_emit32(b.value, emit);
            return 0;
        }
        return asm_fail("unsupported mov form");
    }

    int arithmetic = 0;
    int arith_opcode = 0;
    int digit = 0;
    if (asm_equal(mnemonic, "add")) { arithmetic = 1; arith_opcode = 0x01; digit = 0; }
    else if (asm_equal(mnemonic, "or")) { arithmetic = 1; arith_opcode = 0x09; digit = 1; }
    else if (asm_equal(mnemonic, "and")) { arithmetic = 1; arith_opcode = 0x21; digit = 4; }
    else if (asm_equal(mnemonic, "sub")) { arithmetic = 1; arith_opcode = 0x29; digit = 5; }
    else if (asm_equal(mnemonic, "xor")) { arithmetic = 1; arith_opcode = 0x31; digit = 6; }
    else if (asm_equal(mnemonic, "cmp")) { arithmetic = 1; arith_opcode = 0x39; digit = 7; }
    if (arithmetic) {
        if (argc != 2) return asm_fail("binary instruction needs two operands");
        if (b.type == OP_REG && (a.type == OP_REG || a.type == OP_MEM)) {
            asm_emit8((uint32_t)arith_opcode, emit);
            return asm_emit_rm(&a, b.reg, emit);
        }
        if (b.type == OP_IMM && (a.type == OP_REG || a.type == OP_MEM)) {
            asm_emit8(0x81, emit);
            if (asm_emit_rm(&a, digit, emit) < 0) return -1;
            asm_emit32(b.value, emit);
            return 0;
        }
        return asm_fail("unsupported arithmetic form");
    }
    if (asm_equal(mnemonic, "test")) {
        if (argc != 2 || (a.type != OP_REG && a.type != OP_MEM) ||
            b.type != OP_REG)
            return asm_fail("test needs r/m,reg");
        asm_emit8(0x85, emit);
        return asm_emit_rm(&a, b.reg, emit);
    }
    if (asm_equal(mnemonic, "shl") || asm_equal(mnemonic, "shr")) {
        if (a.type != OP_REG || b.type != OP_IMM)
            return asm_fail("shift needs reg,imm");
        asm_emit8(0xC1, emit);
        asm_emit8((uint32_t)(0xC0 | ((asm_equal(mnemonic, "shl") ? 4 : 5) << 3) |
                             a.reg), emit);
        asm_emit8(b.value, emit);
        return 0;
    }
    if (asm_equal(mnemonic, "jmp") || asm_equal(mnemonic, "call")) {
        if (a.type != OP_IMM) return asm_fail("branch needs label");
        asm_emit8(asm_equal(mnemonic, "jmp") ? 0xE9 : 0xE8, emit);
        uint32_t next = ASM_ORIGIN + (uint32_t)asm_out_len + 4;
        asm_emit32(asm_pass == 1 ? 0 : a.value - next, emit);
        return 0;
    }
    int branch = 0;
    if (asm_branch_opcode(mnemonic, &branch)) {
        if (a.type != OP_IMM) return asm_fail("branch needs label");
        asm_emit8(0x0F, emit);
        asm_emit8((uint32_t)branch, emit);
        uint32_t next = ASM_ORIGIN + (uint32_t)asm_out_len + 4;
        asm_emit32(asm_pass == 1 ? 0 : a.value - next, emit);
        return 0;
    }
    return asm_fail("unknown instruction");
}

static int asm_string(char** text, int emit) {
    char* p = *text;
    if (*p != '"') return -1;
    ++p;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            c = *p++;
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c == '0') c = '\0';
        }
        asm_emit8((uint8_t)c, emit);
    }
    if (*p != '"') return asm_fail("unterminated string");
    *text = p + 1;
    return 0;
}

static int asm_data(char* text, int is_dd, int emit) {
    char temp[256];
    if (k_strlen(text) >= sizeof(temp)) return asm_fail("data too long");
    k_strcpy(temp, text);
    char* p = temp;
    while (*p) {
        p = asm_trim(p);
        if (!*p) break;
        if (asm_string(&p, emit) < 0) {
            if (asm_error[0]) return -1;
            char* end = p;
            int depth = 0;
            while (*end && (*end != ',' || depth)) {
                if (*end == '[') ++depth;
                else if (*end == ']') --depth;
                ++end;
            }
            char saved = *end;
            *end = '\0';
            uint32_t value;
            if (asm_expr(asm_trim(p), &value) < 0) return -1;
            if (is_dd) asm_emit32(value, emit);
            else asm_emit8(value, emit);
            *end = saved;
            p = end;
        }
        p = asm_trim(p);
        if (*p == ',') ++p;
        else if (*p) return asm_fail("expected comma in data");
    }
    return 0;
}

static int asm_process_line(char* line, int emit) {
    char* s = line;
    char* comment = 0;
    char quote = 0;
    for (char* p = line; *p; ++p) {
        if (*p == '"' && quote != '\'') quote = quote ? 0 : '"';
        else if (*p == '\'' && quote != '"') quote = quote ? 0 : '\'';
        else if (*p == ';' && !quote) { comment = p; break; }
    }
    if (comment) *comment = '\0';
    s = asm_trim(s);

    for (;;) {
        s = asm_trim(s);
        char* colon = 0;
        for (char* p = s; *p && !asm_is_space(*p); ++p)
            if (*p == ':') { colon = p; break; }
        if (!colon) break;
        *colon = '\0';
        if (asm_pass == 1 && asm_define_label(s) < 0) return -1;
        s = colon + 1;
    }
    if (!*s) return 0;

    char* mnemonic = s;
    char* rest = mnemonic;
    while (*rest && !asm_is_space(*rest)) ++rest;
    char saved = *rest;
    if (saved) *rest = '\0';
    if (asm_equal(mnemonic, "db") || asm_equal(mnemonic, "dd")) {
        if (saved) *rest = saved;
        rest = asm_trim(rest + (saved ? 1 : 0));
        return asm_data(rest, asm_equal(mnemonic, "dd"), emit);
    }
    if (saved) *rest = saved;
    return asm_encode_instruction(s, emit);
}

static int asm_pass_source(int pass, int emit) {
    asm_pass = pass;
    asm_out_len = 0;
    asm_line_no = 0;
    char* p = asm_src;
    while (*p) {
        ++asm_line_no;
        char* end = p;
        while (*end && *end != '\n') ++end;
        uint32_t len = (uint32_t)(end - p);
        if (len >= 512) return asm_fail("source line too long");
        char work[512];
        k_memcpy(work, p, len);
        work[len] = '\0';
        if (asm_process_line(work, emit) < 0) {
            return -1;
        }
        if (!*end) break;
        p = end + 1;
    }
    if (asm_out_len > ASM_OUT_MAX)
        return asm_fail("output exceeds 16KB");
    return 0;
}

static int asm_read_source(const char* path) {
    int inum = fs_lookup(path);
    if (inum < 0) return asm_fail("source not found");
    if (fs_inode_type((uint32_t)inum) != FS_FILE)
        return asm_fail("source is a directory");
    int used = 0;
    while (used < ASM_SRC_MAX - 1) {
        int n = fs_read((uint32_t)inum, asm_src + used,
                        (uint32_t)used, ASM_SRC_MAX - 1 - (uint32_t)used);
        if (n <= 0) break;
        used += n;
    }
    asm_src[used] = '\0';
    return used ? 0 : asm_fail("source is empty");
}

static int asm_write_output(const char* path) {
    int inum = fs_lookup(path);
    if (inum < 0) inum = fs_create(path, FS_FILE);
    if (inum < 0) return asm_fail("cannot create output");
    if (fs_write((uint32_t)inum, asm_out, (uint32_t)asm_out_len, 0) !=
        asm_out_len)
        return asm_fail("write failed");
    return 0;
}

int asm_run(const char* source_path, const char* output_path) {
    asm_error[0] = '\0';
    asm_label_count = 0;
    serial_puts("[ASM] source=");
    serial_puts(source_path);
    serial_puts("\n\r");
    if (asm_read_source(source_path) < 0) goto fail;
    if (asm_pass_source(1, 0) < 0) goto fail;
    serial_puts("[ASM] pass1 bytes=");
    serial_put_hex((uint32_t)asm_out_len);
    serial_puts("\n\r");
    if (asm_pass_source(2, 1) < 0) goto fail;
    if (asm_write_output(output_path) < 0) goto fail;
    serial_puts("[ASM] saved: ");
    serial_puts(output_path);
    serial_puts(" bytes=");
    serial_put_hex((uint32_t)asm_out_len);
    serial_puts("\n\r");
    return 0;

fail:
    serial_puts("[ASM] error line=");
    serial_put_hex((uint32_t)asm_line_no);
    serial_puts(" msg=");
    serial_puts(asm_error);
    serial_puts("\n\r");
    return -1;
}
