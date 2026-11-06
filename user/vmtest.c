#include "lib.h"

static void line(const char* name, uint32_t value) {
    puts("[VM-TEST] ");
    puts(name);
    puts("=");
    putdec(value);
    puts("\n\r");
}

__attribute__((section(".text.start"))) void _start(void) {
    void* old = brk((void*)VM_HEAP_BASE);
    line("brk-initial", (uint32_t)old == VM_HEAP_BASE);

    void* grown = brk((void*)(VM_HEAP_BASE + 8192));
    line("brk-grow", (uint32_t)grown == VM_HEAP_BASE);

    volatile uint8_t* heap = (volatile uint8_t*)VM_HEAP_BASE;
    heap[0] = 11;
    heap[8191] = 22;

    volatile uint8_t* mapped = (volatile uint8_t*)mmap(8192);
    line("mmap-ok", (uint32_t)mapped == VM_MMAP_BASE);
    if ((uint32_t)mapped == VM_MMAP_BASE) {
        mapped[0] = 33;
        mapped[4096] = 44;
    }

    int inherited = 5;
    int pid = fork();
    if (pid == 0) {
        inherited = 77;
        heap[0] = 99;
        mapped[0] = 88;
        line("child-inherited", (uint32_t)inherited);
        line("child-heap", heap[0]);
        line("child-mmap", mapped[0]);
        exit(0);
    }

    line("fork-pid-positive", pid > 0);
    sleep(10);
    line("parent-inherited", (uint32_t)inherited);
    line("parent-heap", heap[0]);
    line("parent-mmap", mapped[0]);

    int saved_inherited = inherited;
    int saved_heap = heap[0];
    int saved_mmap = mapped[0];
    void* shrunk = brk((void*)VM_HEAP_BASE);
    line("brk-shrink", (uint32_t)shrunk == VM_HEAP_BASE + 8192);

    int ok = pid > 0 && saved_inherited == 5 &&
             saved_heap == 11 && saved_mmap == 33;
    line("done", (uint32_t)ok);
    exit(ok ? 0 : 1);
}
