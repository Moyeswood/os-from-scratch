#include "realmode.h"
#include "types.h"

extern char rm_trampoline_bin[];
extern char rm_trampoline_bin_end[];

void rm_load_trampoline(void) {
    uint32_t *dst = (uint32_t*)0x6000;
    uint32_t *src = (uint32_t*)rm_trampoline_bin;
    uint32_t len = (uint32_t)rm_trampoline_bin_end - (uint32_t)rm_trampoline_bin;
    uint32_t n   = (len + 3) / 4;
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

// 纯汇编跳转函数
extern void rm_jump_to_trampoline(void);
