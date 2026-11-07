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
#define SYS_BRK      8   // 里程碑 13: 设置堆断点, 返回旧断点
#define SYS_MMAP     9   // 里程碑 13: 预留匿名映射, 返回虚拟地址
#define SYS_EXEC     10  // 里程碑 14: 用文件系统镜像替换当前进程
#define SYS_WAITPID  11  // 里程碑 15: 等待 pid/-1, 状态写入 int*
#define SYS_OPEN     12
#define SYS_READ     13
#define SYS_WRITE    14
#define SYS_CLOSE    15
#define SYS_LSEEK    16
#define SYS_CHDIR    17
#define SYS_GETCWD   18
#define SYS_MKDIR    19
#define SYS_PIPE     20
#define SYS_DUP      21
#define SYS_DUP2     22
#define SYS_REMOVE   23
#define SYS_READDIR  24

#define WNOHANG 1

typedef struct {
    uint32_t inum;
    uint32_t type;
    uint32_t size;
    char name[28];
} user_dirent_t;

#define O_RDONLY 0x0001
#define O_WRONLY 0x0002
#define O_RDWR   (O_RDONLY | O_WRONLY)
#define O_CREAT  0x0040
#define O_TRUNC  0x0200
#define O_APPEND 0x0400

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// 与 kernel/vm.h 保持一致的用户虚拟布局。
#define VM_IMAGE_BASE   0x2000000u
#define VM_IMAGE_LIMIT  0x2040000u
#define VM_HEAP_BASE    0x2040000u
#define VM_HEAP_LIMIT   0x20C0000u
#define VM_MMAP_BASE    0x20C0000u
#define VM_MMAP_LIMIT   0x22F0000u
#define VM_STACK_GUARD  0x22F0000u
#define VM_STACK_BOTTOM 0x22F1000u
#define VM_STACK_TOP    0x2400000u

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
static inline void* brk(void* address) {
    return (void*)syscall1(SYS_BRK, (uint32_t)address);
}
static inline void* mmap(uint32_t length) {
    return (void*)syscall1(SYS_MMAP, length);
}
static inline int execve(const char* path, char* const argv[],
                         char* const envp[]) {
    return (int)syscall3(SYS_EXEC, (uint32_t)path, (uint32_t)argv,
                         (uint32_t)envp);
}
static inline int exec(const char* path) {
    return execve(path, 0, 0);
}
static inline int waitpid(int pid, int* status) {
    return (int)syscall3(SYS_WAITPID, (uint32_t)pid, (uint32_t)status, 0);
}
static inline int waitpid_flags(int pid, int* status, int flags) {
    return (int)syscall3(SYS_WAITPID, (uint32_t)pid, (uint32_t)status,
                         (uint32_t)flags);
}
static inline int open(const char* path, uint32_t flags) {
    return (int)syscall3(SYS_OPEN, (uint32_t)path, flags, 0);
}
static inline int read(int fd, void* buf, uint32_t count) {
    return (int)syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, count);
}
static inline int write(int fd, const void* buf, uint32_t count) {
    return (int)syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, count);
}
static inline int close(int fd) {
    return (int)syscall1(SYS_CLOSE, (uint32_t)fd);
}
static inline int lseek(int fd, int offset, int whence) {
    return (int)syscall3(SYS_LSEEK, (uint32_t)fd, (uint32_t)offset,
                         (uint32_t)whence);
}
static inline int chdir(const char* path) {
    return (int)syscall1(SYS_CHDIR, (uint32_t)path);
}
static inline char* getcwd(char* buf, uint32_t size) {
    return (int)syscall3(SYS_GETCWD, (uint32_t)buf, size, 0) == 0 ? buf : 0;
}
static inline int mkdir(const char* path) {
    return (int)syscall1(SYS_MKDIR, (uint32_t)path);
}
static inline int pipe(int fds[2]) {
    return (int)syscall1(SYS_PIPE, (uint32_t)fds);
}
static inline int dup(int fd) {
    return (int)syscall1(SYS_DUP, (uint32_t)fd);
}
static inline int dup2(int oldfd, int newfd) {
    return (int)syscall3(SYS_DUP2, (uint32_t)oldfd, (uint32_t)newfd, 0);
}
static inline int remove(const char* path) {
    return (int)syscall1(SYS_REMOVE, (uint32_t)path);
}
static inline int readdir(const char* path, uint32_t index,
                          user_dirent_t* entry) {
    return (int)syscall3(SYS_READDIR, (uint32_t)path, index,
                         (uint32_t)entry);
}

// 里程碑 12 的 sbrk 接口在里程碑 13 的 brk 之上保持兼容。
// 新 syscall 没有“读取 brk”操作，因此这里维护本程序的镜像值。
static uint32_t user_sbrk_cur = 0;
static inline void* sbrk(int delta) {
    if (!user_sbrk_cur) user_sbrk_cur = VM_HEAP_BASE;
    uint32_t old_addr = user_sbrk_cur;
    if (delta == 0) return (void*)old_addr;

    uint32_t new_addr;
    if (delta > 0) {
        uint32_t increase = (uint32_t)delta;
        if (increase > 0xFFFFFFFFu - old_addr) return (void*)-1;
        new_addr = old_addr + increase;
    } else {
        uint32_t decrease = (uint32_t)(-(delta + 1)) + 1u;
        if (decrease > old_addr) return (void*)-1;
        new_addr = old_addr - decrease;
    }
    if ((uint32_t)brk((void*)new_addr) != old_addr) return (void*)-1;
    user_sbrk_cur = new_addr;
    return (void*)old_addr;
}

// 教学版用户堆分配器 (里程碑 16):
//   - 16 字节块头, 对齐后返回 8 字节对齐的 payload;
//   - 空闲块按地址排序的单链表, malloc 首适配;
//   - free 与相邻空闲块合并, 堆尾空闲块直接 sbrk 收缩;
//   - 分配态/空闲态用 magic 区分, 重复 free 是无操作。
typedef struct user_heap_block {
    uint32_t size;                  // 含块头的总字节数 (8 字节对齐)
    struct user_heap_block* next;   // 仅空闲块使用
    uint32_t magic;
    uint32_t reserved;              // 块头保持 16 字节, payload 仍对齐
} user_heap_block_t;

#define USER_HEAP_ALLOC_MAGIC 0xA110CA7Eu
#define USER_HEAP_FREE_MAGIC  0xF4EEB10Cu

static uint32_t user_heap_cur = 0;
static uint32_t user_heap_base = 0;
static user_heap_block_t* user_heap_free_list = 0;

static uint32_t user_heap_align8(uint32_t n) {
    return (n + 7u) & ~7u;
}

static int user_heap_init(void) {
    if (user_heap_cur) return 1;
    uint32_t brk = (uint32_t)sbrk(0);
    if (brk == 0xFFFFFFFFu) return 0;
    user_heap_base = brk;
    user_heap_cur = brk;
    return 1;
}

static void user_heap_shrink_tail(void) {
    for (;;) {
        user_heap_block_t* prev = 0;
        user_heap_block_t* tail = user_heap_free_list;
        if (!tail) break;
        while (tail->next) {
            prev = tail;
            tail = tail->next;
        }
        if (!tail->size ||
            (uint32_t)tail + tail->size != user_heap_cur ||
            tail->size > 0x7FFFFFFFu) {
            break;
        }
        uint32_t tail_size = tail->size;
        if (prev) prev->next = 0;
        else user_heap_free_list = 0;

        if (sbrk(-(int)tail_size) == (void*)-1) {
            tail->next = 0;
            if (prev) prev->next = tail;
            else user_heap_free_list = tail;
            break;
        }
        user_heap_cur -= tail_size;
    }
}

static void* malloc(uint32_t n) {
    if (n == 0 || !user_heap_init()) return 0;
    if (n > 0xFFFFFFFFu - sizeof(user_heap_block_t)) return 0;

    uint32_t need = user_heap_align8(n + sizeof(user_heap_block_t));
    if (need < n + sizeof(user_heap_block_t)) return 0;

    user_heap_block_t** link = &user_heap_free_list;
    while (*link) {
        user_heap_block_t* block = *link;
        if (block->size >= need) {
            uint32_t rest = block->size - need;
            if (rest >= sizeof(user_heap_block_t) + 8u) {
                user_heap_block_t* split =
                    (user_heap_block_t*)((uint32_t)block + need);
                split->size = rest;
                split->next = block->next;
                split->magic = USER_HEAP_FREE_MAGIC;
                split->reserved = 0;
                *link = split;
                block->size = need;
            } else {
                *link = block->next;
            }
            block->next = 0;
            block->magic = USER_HEAP_ALLOC_MAGIC;
            block->reserved = 0;
            return (void*)(block + 1);
        }
        link = &block->next;
    }

    user_heap_block_t* block = (user_heap_block_t*)sbrk((int)need);
    if (block == (void*)-1) return 0;
    block->size = need;
    block->next = 0;
    block->magic = USER_HEAP_ALLOC_MAGIC;
    block->reserved = 0;
    user_heap_cur = (uint32_t)block + need;
    return (void*)(block + 1);
}

static void free(void* p) {
    if (!p || !user_heap_init()) return;
    uint32_t payload = (uint32_t)p;
    if (payload < user_heap_base + sizeof(user_heap_block_t) ||
        payload >= user_heap_cur || (payload & 7u)) {
        return;
    }

    user_heap_block_t* block =
        (user_heap_block_t*)(payload - sizeof(user_heap_block_t));
    if (block->magic != USER_HEAP_ALLOC_MAGIC || block->size < sizeof(*block)) {
        return;
    }
    block->magic = USER_HEAP_FREE_MAGIC;

    user_heap_block_t* prev = 0;
    user_heap_block_t* next = user_heap_free_list;
    while (next && (uint32_t)next < (uint32_t)block) {
        prev = next;
        next = next->next;
    }
    block->next = next;
    if (prev) prev->next = block;
    else user_heap_free_list = block;

    if (next && (uint32_t)block + block->size == (uint32_t)next) {
        block->size += next->size;
        block->next = next->next;
    }
    if (prev && (uint32_t)prev + prev->size == (uint32_t)block) {
        prev->size += block->size;
        prev->next = block->next;
        block = prev;
    }

    user_heap_shrink_tail();
}

static void* calloc(uint32_t count, uint32_t size) {
    if (size && count > 0xFFFFFFFFu / size) return 0;
    uint32_t total = count * size;
    uint8_t* p = (uint8_t*)malloc(total);
    if (!p) return 0;
    for (uint32_t i = 0; i < total; i++) p[i] = 0;
    return p;
}

static void* realloc(void* p, uint32_t n) {
    if (!p) return malloc(n);
    if (n == 0) {
        free(p);
        return 0;
    }

    user_heap_block_t* old =
        (user_heap_block_t*)((uint32_t)p - sizeof(user_heap_block_t));
    if (old->magic != USER_HEAP_ALLOC_MAGIC || old->size < sizeof(*old)) return 0;
    uint32_t old_capacity = old->size - sizeof(*old);
    if (n <= old_capacity) return p;

    uint8_t* fresh = (uint8_t*)malloc(n);
    if (!fresh) return 0;
    for (uint32_t i = 0; i < old_capacity; i++) fresh[i] = ((uint8_t*)p)[i];
    free(p);
    return fresh;
}
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
