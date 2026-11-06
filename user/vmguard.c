#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    puts("[VM-GUARD] writing stack guard page\n\r");
    *(volatile uint32_t*)VM_STACK_GUARD = 1;
    puts("[VM-GUARD] BUG: guard write survived\n\r");
    exit(0);
}
