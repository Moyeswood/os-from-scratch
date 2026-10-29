#ifndef STRING_H
#define STRING_H

// 内核通用字符串/内存工具 (头文件内联, 无 libc 可用)
// shell.c 里还有一份旧的自用拷贝, 新代码统一用这里。

#include "types.h"

static void k_memset(void* d, uint8_t v, uint32_t n) {
    uint8_t* p = (uint8_t*)d;
    while (n--) *p++ = v;
}

static void k_memcpy(void* d, const void* s, uint32_t n) {
    uint8_t* pd = (uint8_t*)d;
    const uint8_t* ps = (const uint8_t*)s;
    while (n--) *pd++ = *ps++;
}

static int k_memcmp(const void* a, const void* b, uint32_t n) {
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

static uint32_t k_strlen(const char* s) {
    uint32_t n = 0;
    while (*s++) n++;
    return n;
}

static void k_strcpy(char* d, const char* s) {
    while (*s) *d++ = *s++;
    *d = '\0';
}

static int k_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

#endif
