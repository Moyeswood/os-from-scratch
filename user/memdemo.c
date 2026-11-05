#include "lib.h"

// 里程碑 12: 用户堆 + fork 堆隔离 + waitpid 收尸。
// 父进程故意先睡一段时间, 让 shell 在子进程已成为 ZOMBIE 时还能
// 启动别的程序 — 用于验证孤儿清扫不会误收仍有父进程的孩子。

static void line(const char* label, uint32_t value) {
    puts("[MEM] ");
    puts(label);
    puts("=");
    putdec(value);
    putc('\n');
}

__attribute__((section(".text.start"))) void _start(void) {
    void* old = sbrk(0);
    line("break-start", (uint32_t)old);

    void* grown = sbrk(4096);
    uint8_t* page = (uint8_t*)grown;
    line("sbrk-old", (uint32_t)grown);
    line("sbrk-zero", page[0] == 0 && page[4095] == 0);

    char* heap = (char*)malloc(64);
    line("malloc-ok", heap != 0);
    line("malloc-zero", heap && heap[0] == 0 && heap[63] == 0);
    if (heap) heap[0] = 1;
    line("heap-parent-before", heap ? (uint8_t)heap[0] : 0);

    int pid = fork();
    if (pid == 0) {
        if (heap) heap[0] = 7;
        line("child-pid", (uint32_t)getpid());
        line("child-heap", heap ? (uint8_t)heap[0] : 0);
        exit(42);
    }

    line("fork-pid", (uint32_t)pid);
    puts("[MEM] parent-wait-start\n");
    sleep(300);

    int status = -1;
    int reaped = waitpid(-1, &status);
    int child_status = status;
    line("waitpid-pid", (uint32_t)reaped);
    line("waitpid-status", (uint32_t)child_status);
    line("heap-parent-after", heap ? (uint8_t)heap[0] : 0);

    status = 99;
    int none = waitpid(-1, &status);
    line("waitpid-none", (uint32_t)none);
    int badptr = waitpid(-1, (int*)0xB8000);
    line("waitpid-badptr", (uint32_t)badptr);
    void* failed = sbrk(0x7FFFFFFF);
    line("sbrk-overflow", (uint32_t)failed == 0xFFFFFFFFu);

    int ok = pid > 0 && reaped == pid && child_status == 42 &&
             none == -1 && status == 99 &&
             badptr == -1 &&
             heap && heap[0] == 1;
    line("done", (uint32_t)ok);
    exit(ok ? 0 : 1);
}
