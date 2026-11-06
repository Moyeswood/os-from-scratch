#include "lib.h"

// 里程碑 13: 空闲链表 allocator。
// 验证 free 可复用、相邻块合并、堆尾收缩, 以及 calloc/realloc 语义。

static void line(const char* label, uint32_t value) {
    puts("[ALLOC] ");
    puts(label);
    puts("=");
    putdec(value);
    putc('\n');
}

__attribute__((section(".text.start"))) void _start(void) {
    uint32_t initial = (uint32_t)sbrk(0);
    uint8_t* a = (uint8_t*)malloc(64);
    uint8_t* b = (uint8_t*)malloc(64);
    uint8_t* c = (uint8_t*)malloc(64);
    line("alloc-ok", a && b && c);
    line("align-ok",
         (((uint32_t)a | (uint32_t)b | (uint32_t)c) & 7u) == 0);

    if (a) a[0] = 11;
    if (b) b[0] = 22;
    if (c) c[0] = 33;
    uint32_t full_brk = (uint32_t)sbrk(0);

    free(b);
    line("free-keeps-brk", (uint32_t)sbrk(0) == full_brk);

    uint8_t* b2 = (uint8_t*)malloc(64);
    line("free-reuses-block", b2 == b);
    if (b2) b2[0] = 44;
    line("reuse-keeps-brk", (uint32_t)sbrk(0) == full_brk);

    // a 与 b2 相邻; 两个都释放后应合并成一个 160 字节块。
    free(a);
    free(b2);
    uint8_t* joined = (uint8_t*)malloc(112);
    line("adjacent-coalesce", joined == a);
    line("coalesce-keeps-brk", (uint32_t)sbrk(0) == full_brk);

    // 释放整条堆尾后, allocator 应通过负 sbrk 把断点退回起点。
    free(joined);
    free(c);
    line("tail-shrink", (uint32_t)sbrk(0) == initial);

    uint8_t* zeroed = (uint8_t*)calloc(4, 16);
    uint32_t all_zero = zeroed != 0;
    for (uint32_t i = 0; zeroed && i < 64; i++) {
        if (zeroed[i]) all_zero = 0;
    }
    line("calloc-zero", all_zero);
    line("calloc-overflow", calloc(0xFFFFFFFFu, 2) == 0);

    for (uint32_t i = 0; zeroed && i < 64; i++) zeroed[i] = (uint8_t)(i + 1);
    uint8_t* grown = (uint8_t*)realloc(zeroed, 256);
    uint32_t preserved = grown != 0;
    for (uint32_t i = 0; preserved && i < 64; i++) {
        if (grown[i] != (uint8_t)(i + 1)) preserved = 0;
    }
    line("realloc-preserves", preserved);

    uint32_t grown_brk = (uint32_t)sbrk(0);
    uint8_t* same = (uint8_t*)realloc(grown, 64);
    line("realloc-shrinks-in-place", same == grown);
    line("in-place-keeps-brk", same && (uint32_t)sbrk(0) == grown_brk);
    free(same);
    line("realloc-tail-shrink", (uint32_t)sbrk(0) == initial);

    uint8_t* fresh = (uint8_t*)realloc(0, 32);
    line("realloc-null-allocates", fresh != 0);
    line("realloc-zero-frees", realloc(fresh, 0) == 0);
    line("realloc-zero-shrinks", (uint32_t)sbrk(0) == initial);
    free(0);

    // 混合大小块: 释放一批后再申请, 检查块的地址区间不重叠且内容完整。
    uint8_t* blocks[16];
    uint32_t sizes[16];
    uint32_t stress_ok = 1;
    for (uint32_t i = 0; i < 16; i++) {
        sizes[i] = 16u * (i + 1u);
        blocks[i] = (uint8_t*)malloc(sizes[i]);
        if (!blocks[i]) stress_ok = 0;
        for (uint32_t j = 0; blocks[i] && j < sizes[i]; j++) {
            blocks[i][j] = (uint8_t)(i * 17u + j + 1u);
        }
    }
    for (uint32_t i = 0; i < 16; i += 3) {
        free(blocks[i]);
        blocks[i] = (uint8_t*)malloc(sizes[i]);
        if (!blocks[i]) stress_ok = 0;
        for (uint32_t j = 0; blocks[i] && j < sizes[i]; j++) {
            blocks[i][j] = (uint8_t)((i * 17u + j + 1u) ^ 0xA5u);
        }
    }
    for (uint32_t i = 0; i < 16; i++) {
        for (uint32_t j = 0; blocks[i] && j < sizes[i]; j++) {
            uint32_t expected = (i * 17u + j + 1u) ^ ((i % 3) ? 0u : 0xA5u);
            if (blocks[i][j] != (uint8_t)expected) stress_ok = 0;
        }
        for (uint32_t j = i + 1; j < 16; j++) {
            if (!blocks[i] || !blocks[j]) continue;
            uint32_t a0 = (uint32_t)blocks[i];
            uint32_t b0 = (uint32_t)blocks[j];
            if (a0 < b0 + sizes[j] && b0 < a0 + sizes[i]) stress_ok = 0;
        }
    }
    line("stress-integrity", stress_ok);
    for (uint32_t i = 0; i < 16; i++) free(blocks[i]);
    line("stress-shrink", (uint32_t)sbrk(0) == initial);

    int ok = initial != 0xFFFFFFFFu &&
             a && b && c && full_brk > initial &&
             b2 == b && joined == a && all_zero && preserved &&
             same == grown && fresh != 0 && stress_ok &&
             (uint32_t)sbrk(0) == initial;
    line("done", (uint32_t)ok);
    exit(ok ? 0 : 1);
}
