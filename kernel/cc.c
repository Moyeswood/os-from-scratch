// ============================================================
//  c4 迷你 C 编译器 (C in four functions) 移植
//  原作者: Robert Swierczek (github.com/rswier/c4)
//
//  结构忠实原版: next() 词法 → expr() 表达式 (优先级爬升) →
//  stmt() 语句 → main 里的声明解析 + 栈式虚拟机 eval() 解释执行。
//
//  NewOS 适配 (用宏桥接, 主体代码与原文几乎逐字一致):
//   - printf → cc_printf (迷你 printf, %d/%s/%c/%% + 宽度零填充,
//     输出同时到屏幕和串口 — 串口是测试预言机)
//   - malloc/free → 页分配池 (VM 内存不占内核 BSS, 用完释放)
//   - open/read/close → 对接本 OS 文件系统 (编译出的程序能读文件!)
//   - exit(-1) → __builtin_longjmp 回 cc_run 的 setjmp 点
//   - 去掉 -s 源码回显/-d 调试打印 (宿主机特性)
//   - 新增 ESC 失控保护: 每 16384 条指令查一次键盘
// ============================================================

#include "cc.h"
#include "keyboard.h"
#include "string.h"
#include "fs.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "io.h"
#include "types.h"
#include <stdarg.h>

// ---------- 输出: 屏幕 + 串口镜像 ----------
static void cc_putc(char c) {
    vga_putc(c);
    serial_putc(c);
}

// 数字 → 字符串 (无 snprintf)
static void cc_num_to_str(int v, char* out) {
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    int pos = 0;
    if (neg) out[pos++] = '-';
    while (n) out[pos++] = tmp[--n];
    out[pos] = '\0';
}

// 迷你 printf: %d (支持宽度/零填充) %s %c %%
static int cc_printf(const char* fmt, ...) {   // 返回 0 (原版 printf 返回 int)
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { cc_putc(*fmt++); continue; }
        fmt++;
        int zero = 0, width = 0;
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        if (*fmt == 'd') {
            int v = va_arg(ap, int);
            char num[12];
            cc_num_to_str(v, num);
            int len = 0; while (num[len]) len++;
            while (len < width) { cc_putc(zero ? '0' : ' '); len++; }
            cc_printf("%s", num);
        } else if (*fmt == 's') {
            const char* s = va_arg(ap, const char*);
            while (*s) cc_putc(*s++);
        } else if (*fmt == 'c') {
            cc_putc((char)va_arg(ap, int));
        } else if (*fmt == '%') {
            cc_putc('%');
        }
        fmt++;
    }
    va_end(ap);
    return 0;
}

// ---------- 内存池 (页分配, 不占内核 BSS) ----------
#define CC_POOLSZ 65536            // 每个池 64KB (16 页, 必须连续)
#define CC_MAXPOOLS 8
static uint32_t cc_pool_addr[CC_MAXPOOLS];
static uint32_t cc_pool_pages[CC_MAXPOOLS];
static int cc_npools;

static void* cc_palloc(int bytes) {
    uint32_t npages = (uint32_t)((bytes + 4095) / 4096);
    uint32_t base = alloc_pages(npages);   // 连续页 (VM 池是连续地址空间)
    if (base == 0 || cc_npools >= CC_MAXPOOLS) return 0;
    cc_pool_addr[cc_npools] = base;
    cc_pool_pages[cc_npools] = npages;
    cc_npools++;
    return (void*)base;
}

static void cc_pfree_all(void) {
    for (int i = 0; i < cc_npools; i++)
        for (uint32_t j = 0; j < cc_pool_pages[i]; j++)
            free_page(cc_pool_addr[i] + j * 4096);
    cc_npools = 0;
}

// 程序内建 malloc 的堆 (bump 分配器, free 是空操作 — 教学版)
static char* cc_heap;
static char* cc_heap_ptr;

static void* cc_malloc(int bytes) {
    bytes = (bytes + 3) & ~3;
    void* r = cc_heap_ptr;
    cc_heap_ptr += bytes;
    return r;
}

// ---------- 文件系统对接 (编译出的程序可读 OS 文件) ----------
// fd = inode 号; 读取位置按 fd 记录
static uint32_t cc_fd_off[64];

static int cc_open(char* path, int flags) {
    (void)flags;
    int fd = fs_lookup(path);
    if (fd < 0) return -1;
    cc_fd_off[fd & 63] = 0;
    return fd;
}

static int cc_read(int fd, char* buf, int count) {
    if (fd < 0 || count < 0) return -1;
    int r = fs_read((uint32_t)fd, buf, cc_fd_off[fd & 63], (uint32_t)count);
    if (r > 0) cc_fd_off[fd & 63] += (uint32_t)r;
    return r;
}

static int cc_close(int fd) {
    cc_fd_off[fd & 63] = 0;
    return 0;
}

// ---------- 编译器错误退出 (原版 exit(-1) → longjmp) ----------
static void* cc_jmp[8];

#define printf  cc_printf
#define malloc  cc_malloc
#define memset  cc_memset
#define memcmp  k_memcmp
#define open    cc_open
#define read    cc_read
#define close   cc_close
#define exit(x) cc_exit(x)

// k_memset 返回 void, 原版 memset 返回指针 → 包一层
static void* cc_memset(void* d, int v, int n) {
    k_memset(d, (uint8_t)v, (uint32_t)n);
    return d;
}

static void cc_exit(int code);

char *p, *lp,      // current position in source code
     *data;        // data/bss pointer

int *e, *le,       // current position in emitted code
    *id,           // currently parsed identifier
    *sym,          // symbol table (simple list of identifiers)
    tk,            // current token
    ival,          // current token value
    ty,            // current expression type
    loc,           // local variable offset
    line;          // current line number

// tokens and classes (operators last and in precedence order)
enum {
  Num = 128, Fun, Sys, Glo, Loc, Id,
  Char, Else, Enum, If, Int, Return, Sizeof, While, Do, For, Break, Continue,
  Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
};

// opcodes
enum { LEA ,IMM ,JMP ,JSR ,BZ  ,BNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PSH ,
       OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,
       OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT };

// types
enum { CHAR, INT, PTR };

// identifier offsets (since we can't create an ident struct)
enum { Tk, Hash, Name, Class, Type, Val, HClass, HType, HVal, Idsz };

void next()
{
  char *pp;

  while (tk = *p) {
    ++p;
    if (tk == '\n') {
      ++line;
    }
    else if (tk == '#') {
      while (*p != 0 && *p != '\n') ++p;
    }
    else if ((tk >= 'a' && tk <= 'z') || (tk >= 'A' && tk <= 'Z') || tk == '_') {
      pp = p - 1;
      while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')
        tk = tk * 147 + *p++;
      tk = (tk << 6) + (p - pp);
      id = sym;
      while (id[Tk]) {
        if (tk == id[Hash] && !memcmp((char *)id[Name], pp, p - pp)) { tk = id[Tk]; return; }
        id = id + Idsz;
      }
      id[Name] = (int)pp;
      id[Hash] = tk;
      tk = id[Tk] = Id;
      return;
    }
    else if (tk >= '0' && tk <= '9') {
      if (ival = tk - '0') { while (*p >= '0' && *p <= '9') ival = ival * 10 + *p++ - '0'; }
      else if (*p == 'x' || *p == 'X') {
        while ((tk = *++p) && ((tk >= '0' && tk <= '9') || (tk >= 'a' && tk <= 'f') || (tk >= 'A' && tk <= 'F')))
          ival = ival * 16 + (tk & 15) + (tk >= 'A' ? 9 : 0);
      }
      else { while (*p >= '0' && *p <= '7') ival = ival * 8 + *p++ - '0'; }
      tk = Num;
      return;
    }
    else if (tk == '/') {
      if (*p == '/') {
        ++p;
        while (*p != 0 && *p != '\n') ++p;
      }
      else {
        tk = Div;
        return;
      }
    }
    else if (tk == '\'' || tk == '"') {
      pp = data;
      while (*p != 0 && *p != tk) {
        if ((ival = *p++) == '\\') {
          if ((ival = *p++) == 'n') ival = '\n';
        }
        if (tk == '"') *data++ = ival;
      }
      ++p;
      if (tk == '"') ival = (int)pp; else tk = Num;
      return;
    }
    else if (tk == '=') { if (*p == '=') { ++p; tk = Eq; } else tk = Assign; return; }
    else if (tk == '+') { if (*p == '+') { ++p; tk = Inc; } else tk = Add; return; }
    else if (tk == '-') { if (*p == '-') { ++p; tk = Dec; } else tk = Sub; return; }
    else if (tk == '!') { if (*p == '=') { ++p; tk = Ne; } return; }
    else if (tk == '<') { if (*p == '=') { ++p; tk = Le; } else if (*p == '<') { ++p; tk = Shl; } else tk = Lt; return; }
    else if (tk == '>') { if (*p == '=') { ++p; tk = Ge; } else if (*p == '>') { ++p; tk = Shr; } else tk = Gt; return; }
    else if (tk == '|') { if (*p == '|') { ++p; tk = Lor; } else tk = Or; return; }
    else if (tk == '&') { if (*p == '&') { ++p; tk = Lan; } else tk = And; return; }
    else if (tk == '^') { tk = Xor; return; }
    else if (tk == '%') { tk = Mod; return; }
    else if (tk == '*') { tk = Mul; return; }
    else if (tk == '[') { tk = Brak; return; }
    else if (tk == '?') { tk = Cond; return; }
    else if (tk == '~' || tk == ';' || tk == '{' || tk == '}' || tk == '(' || tk == ')' || tk == ']' || tk == ',' || tk == ':') return;
  }
}

void expr(int lev)
{
  int t, *d;

  if (!tk) { printf("%d: unexpected eof in expression\n", line); exit(-1); }
  else if (tk == Num) { *++e = IMM; *++e = ival; next(); ty = INT; }
  else if (tk == '"') {
    *++e = IMM; *++e = ival; next();
    while (tk == '"') next();
    data = (char *)((int)data + sizeof(int) & -sizeof(int)); ty = PTR;
  }
  else if (tk == Sizeof) {
    next(); if (tk == '(') next(); else { printf("%d: open paren expected in sizeof\n", line); exit(-1); }
    ty = INT; if (tk == Int) next(); else if (tk == Char) { next(); ty = CHAR; }
    while (tk == Mul) { next(); ty = ty + PTR; }
    if (tk == ')') next(); else { printf("%d: close paren expected in sizeof\n", line); exit(-1); }
    *++e = IMM; *++e = (ty == CHAR) ? sizeof(char) : sizeof(int);
    ty = INT;
  }
  else if (tk == Id) {
    d = id; next();
    if (tk == '(') {
      next();
      t = 0;
      while (tk != ')') { expr(Assign); *++e = PSH; ++t; if (tk == ',') next(); }
      next();
      if (d[Class] == Sys) *++e = d[Val];
      else if (d[Class] == Fun) { *++e = JSR; *++e = d[Val]; }
      else { printf("%d: bad function call\n", line); exit(-1); }
      if (t) { *++e = ADJ; *++e = t; }
      ty = d[Type];
    }
    else if (d[Class] == Num) { *++e = IMM; *++e = d[Val]; ty = INT; }
    else {
      if (d[Class] == Loc) { *++e = LEA; *++e = loc - d[Val]; }
      else if (d[Class] == Glo) { *++e = IMM; *++e = d[Val]; }
      else { printf("%d: undefined variable\n", line); exit(-1); }
      *++e = ((ty = d[Type]) == CHAR) ? LC : LI;
    }
  }
  else if (tk == '(') {
    next();
    if (tk == Int || tk == Char) {
      t = (tk == Int) ? INT : CHAR; next();
      while (tk == Mul) { next(); t = t + PTR; }
      if (tk == ')') next(); else { printf("%d: bad cast\n", line); exit(-1); }
      expr(Inc);
      ty = t;
    }
    else {
      expr(Assign);
      if (tk == ')') next(); else { printf("%d: close paren expected\n", line); exit(-1); }
    }
  }
  else if (tk == Mul) {
    next(); expr(Inc);
    if (ty > INT) ty = ty - PTR; else { printf("%d: bad dereference\n", line); exit(-1); }
    *++e = (ty == CHAR) ? LC : LI;
  }
  else if (tk == And) {
    next(); expr(Inc);
    if (*e == LC || *e == LI) --e; else { printf("%d: bad address-of\n", line); exit(-1); }
    ty = ty + PTR;
  }
  else if (tk == '!') { next(); expr(Inc); *++e = PSH; *++e = IMM; *++e = 0; *++e = EQ; ty = INT; }
  else if (tk == '~') { next(); expr(Inc); *++e = PSH; *++e = IMM; *++e = -1; *++e = XOR; ty = INT; }
  else if (tk == Add) { next(); expr(Inc); ty = INT; }
  else if (tk == Sub) {
    next(); *++e = IMM;
    if (tk == Num) { *++e = -ival; next(); } else { *++e = -1; *++e = PSH; expr(Inc); *++e = MUL; }
    ty = INT;
  }
  else if (tk == Inc || tk == Dec) {
    t = tk; next(); expr(Inc);
    if (*e == LC) { *e = PSH; *++e = LC; }
    else if (*e == LI) { *e = PSH; *++e = LI; }
    else { printf("%d: bad lvalue in pre-increment\n", line); exit(-1); }
    *++e = PSH;
    *++e = IMM; *++e = (ty > PTR) ? sizeof(int) : sizeof(char);
    *++e = (t == Inc) ? ADD : SUB;
    *++e = (ty == CHAR) ? SC : SI;
  }
  else { printf("%d: bad expression\n", line); exit(-1); }

  while (tk >= lev) { // "precedence climbing" or "Top Down Operator Precedence" method
    t = ty;
    if (tk == Assign) {
      next();
      if (*e == LC || *e == LI) *e = PSH; else { printf("%d: bad lvalue in assignment\n", line); exit(-1); }
      expr(Assign); *++e = ((ty = t) == CHAR) ? SC : SI;
    }
    else if (tk == Cond) {
      next();
      *++e = BZ; d = ++e;
      expr(Assign);
      if (tk == ':') next(); else { printf("%d: conditional missing colon\n", line); exit(-1); }
      *d = (int)(e + 3); *++e = JMP; d = ++e;
      expr(Cond);
      *d = (int)(e + 1);
    }
    else if (tk == Lor) { next(); *++e = BNZ; d = ++e; expr(Lan); *d = (int)(e + 1); ty = INT; }
    else if (tk == Lan) { next(); *++e = BZ;  d = ++e; expr(Or);  *d = (int)(e + 1); ty = INT; }
    else if (tk == Or)  { next(); *++e = PSH; expr(Xor); *++e = OR;  ty = INT; }
    else if (tk == Xor) { next(); *++e = PSH; expr(And); *++e = XOR; ty = INT; }
    else if (tk == And) { next(); *++e = PSH; expr(Eq);  *++e = AND; ty = INT; }
    else if (tk == Eq)  { next(); *++e = PSH; expr(Lt);  *++e = EQ;  ty = INT; }
    else if (tk == Ne)  { next(); *++e = PSH; expr(Lt);  *++e = NE;  ty = INT; }
    else if (tk == Lt)  { next(); *++e = PSH; expr(Shl); *++e = LT;  ty = INT; }
    else if (tk == Gt)  { next(); *++e = PSH; expr(Shl); *++e = GT;  ty = INT; }
    else if (tk == Le)  { next(); *++e = PSH; expr(Shl); *++e = LE;  ty = INT; }
    else if (tk == Ge)  { next(); *++e = PSH; expr(Shl); *++e = GE;  ty = INT; }
    else if (tk == Shl) { next(); *++e = PSH; expr(Add); *++e = SHL; ty = INT; }
    else if (tk == Shr) { next(); *++e = PSH; expr(Add); *++e = SHR; ty = INT; }
    else if (tk == Add) {
      next(); *++e = PSH; expr(Mul);
      if ((ty = t) > PTR) { *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = MUL;  }
      *++e = ADD;
    }
    else if (tk == Sub) {
      next(); *++e = PSH; expr(Mul);
      if (t > PTR && t == ty) { *++e = SUB; *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = DIV; ty = INT; }
      else if ((ty = t) > PTR) { *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = MUL; *++e = SUB; }
      else *++e = SUB;
    }
    else if (tk == Mul) { next(); *++e = PSH; expr(Inc); *++e = MUL; ty = INT; }
    else if (tk == Div) { next(); *++e = PSH; expr(Inc); *++e = DIV; ty = INT; }
    else if (tk == Mod) { next(); *++e = PSH; expr(Inc); *++e = MOD; ty = INT; }
    else if (tk == Inc || tk == Dec) {
      if (*e == LC) { *e = PSH; *++e = LC; }
      else if (*e == LI) { *e = PSH; *++e = LI; }
      else { printf("%d: bad lvalue in post-increment\n", line); exit(-1); }
      *++e = PSH; *++e = IMM; *++e = (ty > PTR) ? sizeof(int) : sizeof(char);
      *++e = (tk == Inc) ? ADD : SUB;
      *++e = (ty == CHAR) ? SC : SI;
      *++e = PSH; *++e = IMM; *++e = (ty > PTR) ? sizeof(int) : sizeof(char);
      *++e = (tk == Inc) ? SUB : ADD;
      next();
    }
    else if (tk == Brak) {
      next(); *++e = PSH; expr(Assign);
      if (tk == ']') next(); else { printf("%d: close bracket expected\n", line); exit(-1); }
      if (t > PTR) { *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = MUL;  }
      else if (t < PTR) { printf("%d: pointer type expected\n", line); exit(-1); }
      *++e = ADD;
      *++e = ((ty = t - PTR) == CHAR) ? LC : LI;
    }
    else { printf("%d: compiler error tk=%d\n", line, tk); exit(-1); }
  }
}

// for 增量子句先在这里编译, 循环体后拷回代码流。按嵌套深度分层 —
// 内层 for 会递归使用更深一层, 互不覆盖。
// ★局限: 暂存代码里的跳转 (&& || ?:) 目标指向暂存区, 拷回后会失效 —
// 教学版约定: for 增量表达式里不要用 && || ?:。
static int cc_scratch[32][256];
static int cc_scratch_depth;
// break/continue 的 JMP 操作数补丁槽 (循环嵌套用计数保存/恢复)
static int cc_brk[32], cc_brk_n;
static int cc_cnt[32], cc_cnt_n;
// 全局初始化代码 (编译期累积, 解析完后拷到代码流末尾作为执行入口)
static int cc_ginit[512];
static int cc_ginit_n;

void stmt()
{
  int *a, *b;
  int i, save_brk, save_cnt;

  if (tk == If) {
    next();
    if (tk == '(') next(); else { printf("%d: open paren expected\n", line); exit(-1); }
    expr(Assign);
    if (tk == ')') next(); else { printf("%d: close paren expected\n", line); exit(-1); }
    *++e = BZ; b = ++e;
    stmt();
    if (tk == Else) {
      *b = (int)(e + 3); *++e = JMP; b = ++e;
      next();
      stmt();
    }
    *b = (int)(e + 1);
  }
  else if (tk == While) {
    next();
    save_brk = cc_brk_n; save_cnt = cc_cnt_n;
    a = e + 1;
    if (tk == '(') next(); else { printf("%d: open paren expected\n", line); exit(-1); }
    expr(Assign);
    if (tk == ')') next(); else { printf("%d: close paren expected\n", line); exit(-1); }
    *++e = BZ; b = ++e;
    stmt();
    *++e = JMP; *++e = (int)a;
    *b = (int)(e + 1);
    while (cc_brk_n > save_brk) { cc_brk_n--; *(int *)cc_brk[cc_brk_n] = (int)(e + 1); }
    while (cc_cnt_n > save_cnt) { cc_cnt_n--; *(int *)cc_cnt[cc_cnt_n] = (int)a; }
  }
  else if (tk == Do) {
    next();
    save_brk = cc_brk_n; save_cnt = cc_cnt_n;
    a = e + 1;                                   // 循环体起点
    stmt();
    if (tk != While) { printf("%d: while expected after do body\n", line); exit(-1); }
    next();
    if (tk == '(') next(); else { printf("%d: open paren expected\n", line); exit(-1); }
    b = e + 1;                                   // 条件起点 (continue 目标)
    expr(Assign);
    if (tk == ')') next(); else { printf("%d: close paren expected\n", line); exit(-1); }
    *++e = BNZ; *++e = (int)a;                   // 条件真 → 回循环体
    while (cc_brk_n > save_brk) { cc_brk_n--; *(int *)cc_brk[cc_brk_n] = (int)(e + 1); }
    while (cc_cnt_n > save_cnt) { cc_cnt_n--; *(int *)cc_cnt[cc_cnt_n] = (int)b; }
    if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
  }
  else if (tk == For) {
    next();
    if (tk == '(') next(); else { printf("%d: open paren expected\n", line); exit(-1); }
    if (tk != ';') expr(Assign);                 // 初始化子句 (进循环前跑一次)
    if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
    a = e + 1;                                   // 条件重估位置
    if (tk != ';') expr(Assign);                 // 条件
    if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
    *++e = BZ; b = ++e;                          // 条件假 → 循环尾 (槽在 b)
    save_brk = cc_brk_n; save_cnt = cc_cnt_n;
    if (cc_scratch_depth >= 32) { printf("%d: for nested too deep\n", line); exit(-1); }
    {
      // 增量表达式: 代码暂存到本层 scratch (语法在前, 代码要放到循环体后)
      // ★e 从 sc-1 起: *++e 的第一笔写落在 sc[0], 长度/拷贝才对齐;
      // ★depth 在 body 解析期间保持占用: 内层 for 用更深一层, 不会覆盖本层暂存
      int *sc = cc_scratch[cc_scratch_depth++];
      int *save = e;
      int *sbase = sc - 1;
      e = sbase;
      if (tk != ')') expr(Assign);
      i = (int)(e - sbase);                      // 增量代码字数
      e = save;
      if (tk == ')') next(); else { printf("%d: close paren expected\n", line); exit(-1); }
      stmt();                                    // 循环体
      cc_scratch_depth--;                        // 释放本层
      {
        int *incr = e + 1;                       // 增量起点 (= continue 目标)
        int j;
        for (j = 0; j < i; j++) *++e = sc[j];    // 拷回增量代码
        *++e = JMP; *++e = (int)a;               // 回条件
        *b = (int)(e + 1);                       // 条件假 → 循环尾
        while (cc_brk_n > save_brk) { cc_brk_n--; *(int *)cc_brk[cc_brk_n] = (int)(e + 1); }
        while (cc_cnt_n > save_cnt) { cc_cnt_n--; *(int *)cc_cnt[cc_cnt_n] = (int)incr; }
      }
    }
  }
  else if (tk == Break) {
    next();
    if (cc_brk_n >= 32) { printf("%d: break nested too deep\n", line); exit(-1); }
    *++e = JMP; cc_brk[cc_brk_n++] = (int)(e + 1); *++e = 0;
    if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
  }
  else if (tk == Continue) {
    next();
    if (cc_cnt_n >= 32) { printf("%d: continue nested too deep\n", line); exit(-1); }
    *++e = JMP; cc_cnt[cc_cnt_n++] = (int)(e + 1); *++e = 0;
    if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
  }
  else if (tk == Return) {
    next();
    if (tk != ';') expr(Assign);
    *++e = LEV;
    if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
  }
  else if (tk == '{') {
    next();
    while (tk != '}') stmt();
    next();
  }
  else if (tk == ';') {
    next();
  }
  else {
    expr(Assign);
    if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
  }
}

static char cc_srcbuf[16384];    // 源文件缓冲 (静态, 16KB 够教学程序)

static void cc_exit(int code) {
    // 原版 exit(-1) → 跳回 cc_run 的 setjmp 点
    (void)code;
    __builtin_longjmp(cc_jmp, 1);   // clang 要求常量; 错误路径统一返回 -1
}

int cc_run(const char* path, int argc, char** argv)
{
  int fd, bt, poolsz, *idmain;
  int *pc, *sp, *bp, a, cycle; // vm registers
  int i, *t; // temps

  // 编译错误/提前退出都从这里返回 (cc_exit longjmp 到此处)
  if (__builtin_setjmp(cc_jmp) != 0) {
      cc_pfree_all();
      return -1;
  }

  // 分配 VM 内存池 (页分配: 不占 BSS, 用完释放)
  poolsz = CC_POOLSZ;
  if (!(sym = cc_palloc(poolsz))) { printf("out of memory (symbol pool)\n"); cc_pfree_all(); return -1; }
  if (!(le = e = cc_palloc(poolsz))) { printf("out of memory (text pool)\n"); cc_pfree_all(); return -1; }
  if (!(data = cc_palloc(poolsz)))  { printf("out of memory (data pool)\n");  cc_pfree_all(); return -1; }
  if (!(sp = cc_palloc(poolsz)))    { printf("out of memory (stack pool)\n"); cc_pfree_all(); return -1; }
  if (!(cc_heap = cc_palloc(poolsz))) { printf("out of memory (heap pool)\n"); cc_pfree_all(); return -1; }
  cc_heap_ptr = cc_heap;

  memset(sym,  0, poolsz);
  memset(e,    0, poolsz);
  memset(data, 0, poolsz);
  for (i = 0; i < 64; i++) cc_fd_off[i] = 0;
  cc_scratch_depth = 0; cc_brk_n = 0; cc_cnt_n = 0; cc_ginit_n = 0;

  // 从文件系统读源文件
  serial_puts("[CC] compile: "); serial_puts(path); serial_puts("\n\r");
  if ((fd = fs_lookup(path)) < 0) { printf("could not open(%s)\n", path); cc_pfree_all(); return -1; }
  {
    char probe[1];
    if (fs_read((uint32_t)fd, probe, 0, 1) < 0) { printf("could not open(%s): is a directory\n", path); cc_pfree_all(); return -1; }
  }
  {
    uint32_t off = 0;
    int r;
    while ((r = fs_read((uint32_t)fd, cc_srcbuf + off, off, 4096)) > 0) {
      off += (uint32_t)r;
      if (off >= sizeof(cc_srcbuf) - 1) break;
    }
    cc_srcbuf[off] = 0;
    p = cc_srcbuf;
  }

  p = "char else enum if int return sizeof while do for break continue "
      "open read close printf malloc free memset memcmp exit void main";
  i = Char; while (i <= Continue) { next(); id[Tk] = i++; } // add keywords to symbol table
  i = OPEN; while (i <= EXIT) { next(); id[Class] = Sys; id[Type] = INT; id[Val] = i++; } // add library to symbol table
  next(); id[Tk] = Char; // handle void type
  next(); idmain = id; // keep track of main

  // 换回真正的源文件 (关键字表已建好)
  p = cc_srcbuf;

  // parse declarations
  line = 1;
  next();
  while (tk) {
    bt = INT; // basetype
    if (tk == Int) next();
    else if (tk == Char) { next(); bt = CHAR; }
    else if (tk == Enum) {
      next();
      if (tk != '{') next();
      if (tk == '{') {
        next();
        i = 0;
        while (tk != '}') {
          if (tk != Id) { printf("%d: bad enum identifier %d\n", line, tk); exit(-1); }
          next();
          if (tk == Assign) {
            next();
            if (tk != Num) { printf("%d: bad enum initializer\n", line); exit(-1); }
            i = ival;
            next();
          }
          id[Class] = Num; id[Type] = INT; id[Val] = i++;
          if (tk == ',') next();
        }
        next();
      }
    }
    while (tk != ';' && tk != '}') {
      ty = bt;
      while (tk == Mul) { next(); ty = ty + PTR; }
      if (tk != Id) { printf("%d: bad global declaration\n", line); exit(-1); }
      if (id[Class]) { printf("%d: duplicate global definition\n", line); exit(-1); }
      next();
      id[Type] = ty;
      if (tk == '(') { // function
        id[Class] = Fun;
        id[Val] = (int)(e + 1);
        next(); i = 0;
        while (tk != ')') {
          ty = INT;
          if (tk == Int) next();
          else if (tk == Char) { next(); ty = CHAR; }
          while (tk == Mul) { next(); ty = ty + PTR; }
          if (tk != Id) { printf("%d: bad parameter declaration\n", line); exit(-1); }
          if (id[Class] == Loc) { printf("%d: duplicate parameter definition\n", line); exit(-1); }
          id[HClass] = id[Class]; id[Class] = Loc;
          id[HType]  = id[Type];  id[Type] = ty;
          id[HVal]   = id[Val];   id[Val] = i++;
          next();
          if (tk == ',') next();
        }
        next();
        if (tk != '{') { printf("%d: bad function definition\n", line); exit(-1); }
        loc = ++i;
        next();
        // ★ENT 必须在声明解析前发出: 声明初始化代码要落在函数入口之后
        // (否则初始化会在 ENT 建帧前执行, 写到调用者的栈上)。
        *++e = ENT; *++e = 0; t = e;               // 局部数占位, 声明完后回填
        while (tk == Int || tk == Char) {
          bt = (tk == Int) ? INT : CHAR;
          next();
          while (tk != ';') {
            ty = bt;
            while (tk == Mul) { next(); ty = ty + PTR; }
            if (tk != Id) { printf("%d: bad local declaration\n", line); exit(-1); }
            if (id[Class] == Loc) { printf("%d: duplicate local definition\n", line); exit(-1); }
            id[HClass] = id[Class]; id[Class] = Loc;
            id[HType]  = id[Type];  id[Type] = ty;
            id[HVal]   = id[Val];   id[Val] = ++i;
            next();
            if (tk == Assign) {
              // 局部声明初始化: LEA 取地址 → PSH → 值 → SC/SI 存回
              // ★先存槽位/类型: next() 越过 '=' 会把 id/ty 重解析成右侧表达式
              int v = id[Val];
              int dty = ty;
              next();
              *++e = LEA; *++e = loc - v; *++e = PSH;
              expr(Assign);
              *++e = (dty == CHAR) ? SC : SI;
            }
            if (tk == ',') next();
          }
          next();
        }
        *t = i - loc;                              // 回填 ENT 的局部数
        while (tk != '}') stmt();
        *++e = LEV;
        id = sym; // unwind symbol table locals
        while (id[Tk]) {
          if (id[Class] == Loc) {
            id[Class] = id[HClass];
            id[Type] = id[HType];
            id[Val] = id[HVal];
          }
          id = id + Idsz;
        }
      }
      else {
        id[Class] = Glo;
        id[Val] = (int)data;
        data = data + sizeof(int);
      }
      if (tk == Assign) {
        // 全局初始化: 代码暂存到 scratch, 解析完后拷到代码流末尾
        // (执行入口设在那里: 跑完初始化 JMP main)。局限同 for 增量。
        // ★e 从 sc-1 起: *++e 的第一笔写落在 sc[0], 长度/拷贝才对齐;
        // ★先存地址/类型: next() 越过 '=' 会把 id/ty 重解析成右侧表达式
        int *save = e;
        int *sbase = cc_scratch[0] - 1;
        int gaddr = id[Val];
        int dty = ty;
        e = sbase;
        next();
        *++e = IMM; *++e = gaddr; *++e = PSH;
        expr(Assign);
        *++e = (dty == CHAR) ? SC : SI;
        i = (int)(e - sbase);                    // 初始化代码字数
        e = save;
        {
          int j;
          for (j = 0; j < i; j++) cc_ginit[cc_ginit_n + j] = cc_scratch[0][j];
          cc_ginit_n += i;
        }
      }
      if (tk == ',') next();
    }
    next();
  }

  if (!(pc = (int *)idmain[Val])) { printf("main() not defined\n"); cc_pfree_all(); return -1; }

  // 全局初始化: 拷到代码流末尾, 执行入口设在这里 (跑完 JMP main)。
  // main 的 LEV 照常弹出 EXIT 返回地址 → 退出路径不变。
  if (cc_ginit_n > 0) {
    int *gstart = e + 1;
    int j;
    for (j = 0; j < cc_ginit_n; j++) *++e = cc_ginit[j];
    *++e = JMP; *++e = (int)pc;
    pc = gstart;
  }

  // setup stack
  bp = sp = (int *)((int)sp + poolsz);
  *--sp = EXIT; // call exit if main returns
  *--sp = PSH; t = sp;
  *--sp = argc;
  *--sp = (int)argv;
  *--sp = (int)t;

  // run...
  cycle = 0;
  while (1) {
    i = *pc++; ++cycle;

    // ★ESC 失控保护: 每 16384 条指令查一次键盘环形缓冲
    // (里程碑 11a: 老版直接读 8042 会吃掉用户的其它按键;
    // 新版本非破坏扫描, ESC 之外的键留给 shell 做 type-ahead)
    if ((cycle & 0x3FFF) == 0 && kbd_check_esc()) {
        printf("interrupted at cycle %d\n", cycle);
        cc_pfree_all();
        return -2;
    }

    if      (i == LEA) a = (int)(bp + *pc++);                             // load local address
    else if (i == IMM) a = *pc++;                                         // load global address or immediate
    else if (i == JMP) pc = (int *)*pc;                                   // jump
    else if (i == JSR) { *--sp = (int)(pc + 1); pc = (int *)*pc; }        // jump to subroutine
    else if (i == BZ)  pc = a ? pc + 1 : (int *)*pc;                      // branch if zero
    else if (i == BNZ) pc = a ? (int *)*pc : pc + 1;                      // branch if not zero
    else if (i == ENT) { *--sp = (int)bp; bp = sp; sp = sp - *pc++; }     // enter subroutine
    else if (i == ADJ) sp = sp + *pc++;                                   // stack adjust
    else if (i == LEV) { sp = bp; bp = (int *)*sp++; pc = (int *)*sp++; } // leave subroutine
    else if (i == LI)  a = *(int *)a;                                     // load int
    else if (i == LC)  a = *(char *)a;                                    // load char
    else if (i == SI)  *(int *)*sp++ = a;                                 // store int
    else if (i == SC)  a = *(char *)*sp++ = a;                            // store char
    else if (i == PSH) *--sp = a;                                         // push

    else if (i == OR)  a = *sp++ |  a;
    else if (i == XOR) a = *sp++ ^  a;
    else if (i == AND) a = *sp++ &  a;
    else if (i == EQ)  a = *sp++ == a;
    else if (i == NE)  a = *sp++ != a;
    else if (i == LT)  a = *sp++ <  a;
    else if (i == GT)  a = *sp++ >  a;
    else if (i == LE)  a = *sp++ <= a;
    else if (i == GE)  a = *sp++ >= a;
    else if (i == SHL) a = *sp++ << a;
    else if (i == SHR) a = *sp++ >> a;
    else if (i == ADD) a = *sp++ +  a;
    else if (i == SUB) a = *sp++ -  a;
    else if (i == MUL) a = *sp++ *  a;
    else if (i == DIV) a = *sp++ /  a;
    else if (i == MOD) a = *sp++ %  a;

    else if (i == OPEN) a = open((char *)sp[1], *sp);
    else if (i == READ) a = read(sp[2], (char *)sp[1], *sp);
    else if (i == CLOS) a = close(*sp);
    else if (i == PRTF) { t = sp + pc[1]; a = printf((char *)t[-1], t[-2], t[-3], t[-4], t[-5], t[-6]); }
    else if (i == MALC) a = (int)malloc(*sp);
    else if (i == FREE) a = *sp;      // bump 堆的 free 是空操作 (教学版)
    else if (i == MSET) a = (int)memset((char *)sp[2], sp[1], *sp);
    else if (i == MCMP) a = memcmp((char *)sp[2], (char *)sp[1], *sp);
    else if (i == EXIT) { printf("exit(%d) cycle = %d\n", *sp, cycle); cc_pfree_all(); return *sp; }
    else { printf("unknown instruction = %d! cycle = %d\n", i, cycle); cc_pfree_all(); return -1; }
  }
}
