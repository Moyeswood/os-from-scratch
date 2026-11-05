#ifndef USER_LIB_H
#define USER_LIB_H

// ============================================================
//  用户态程序库 (user/*.c 共用)
//
//  铁律: 用户程序不能直接调用内核函数 — 内核代码页是
//  supervisor (U/S=0), 用户态一执行就 #PF。所有内核服务
//  都走 int 0x80 系统调用 (门 DPL=3, 用户态唯一的大门)。
//
//  系统调用号必须和 kernel/user.h 的 SYS_* 一致。
// ============================================================

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;

static inline uint32_t syscall3(uint32_t n, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}
static inline uint32_t syscall1(uint32_t n, uint32_t a) { return syscall3(n, a, 0, 0); }
static inline uint32_t syscall0(uint32_t n) { return syscall3(n, 0, 0, 0); }

#define SYS_EXIT     0   // 结束进程, 参数 = 退出码
#define SYS_PUTC     1   // 输出一个字符 (屏幕 + 串口)
#define SYS_PUTS     2   // 输出字符串 (指针必须指向用户区, 内核会校验)
#define SYS_GETKEY   3   // 读一个键 (阻塞; ESC 返回 27)
#define SYS_GETTICKS 4   // 读系统滴答 (IRQ0 定时器计数)
#define SYS_FORK     5   // 里程碑 11b: 复制地址空间+现场, 父得子 pid, 子得 0
#define SYS_GETPID   6   // 里程碑 11b: 读自己的 pid
#define SYS_SLEEP    7   // 里程碑 11b: 睡 N 个滴答 (~55ms 每滴答)
#define SYS_SBRK     8   // 里程碑 12: 调整堆顶, 返回旧断点 ((void*)-1=失败)
#define SYS_WAITPID  9   // 里程碑 12: 等 pid (或 -1), 状态写到第二个参数

// exit 不会返回 (进程变 ZOMBIE 被切走); 写个死循环兜底
static inline void exit(int code) {
    syscall1(SYS_EXIT, (uint32_t)code);
    for (;;) {}
}

static inline void putc(char c) { syscall1(SYS_PUTC, (uint32_t)(uint8_t)c); }
static inline int  puts(const char* s) { return (int)syscall1(SYS_PUTS, (uint32_t)s); }
static inline int  getkey(void) { return (int)syscall1(SYS_GETKEY, 0); }
static inline uint32_t ticks(void) { return syscall0(SYS_GETTICKS); }
static inline int  fork(void) { return (int)syscall0(SYS_FORK); }
static inline int  getpid(void) { return (int)syscall0(SYS_GETPID); }
static inline void sleep(uint32_t n) { syscall1(SYS_SLEEP, n); }
static inline void* sbrk(int delta) { return (void*)syscall1(SYS_SBRK, (uint32_t)delta); }
static inline int waitpid(int pid, int* status) {
    return (int)syscall3(SYS_WAITPID, (uint32_t)pid, (uint32_t)status, 0);
}

// 教学版用户堆分配器: 8 字节对齐的 bump allocator。free 是空操作 —
// 本项目的短生命周期程序足够用; 真正回收留给后续 allocator 里程碑。
static uint32_t user_heap_cur = 0;

static void* malloc(uint32_t n) {
    if (n == 0) return 0;
    if (!user_heap_cur) user_heap_cur = (uint32_t)sbrk(0);
    if (user_heap_cur == 0xFFFFFFFFu) return 0;
    uint32_t start = (user_heap_cur + 7u) & ~7u;
    if (n > 0xFFFFFFFFu - start) return 0;
    uint32_t next = start + n;
    next = (next + 7u) & ~7u;
    if (next < start || sbrk((int)(next - user_heap_cur)) == (void*)-1) return 0;
    user_heap_cur = next;
    return (void*)start;
}

static void free(void* p) { (void)p; }

// 十六进制打印 (8 位定宽, 和内核 serial_put_hex 同款格式, 测试好匹配)
static void puthex(uint32_t n) {
    puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint32_t nib = (n >> i) & 0xF;
        putc((char)(nib < 10 ? '0' + nib : 'A' + nib - 10));
    }
}

// 十进制打印 (count 演示用)
static void putdec(uint32_t n) {
    if (n == 0) { putc('0'); return; }
    char tmp[12];
    int pos = 0;
    while (n) { tmp[pos++] = (char)('0' + n % 10); n /= 10; }
    while (pos) putc(tmp[--pos]);
}

#endif
