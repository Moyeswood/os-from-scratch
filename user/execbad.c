#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    puts("[EXEC-BAD] testing missing executable\n\r");
    int missing = exec("/bin/does-not-exist");
    puts("[EXEC-BAD] missing-return=");
    putdec((uint32_t)missing == 0xFFFFFFFFu);
    puts("\n\r");

    puts("[EXEC-BAD] testing kernel path pointer\n\r");
    int badptr = exec((const char*)0xB8000);
    puts("[EXEC-BAD] badptr-return=");
    putdec((uint32_t)badptr == 0xFFFFFFFFu);
    puts("\n\r");

    int ok = missing == -1 && badptr == -1;
    puts("[EXEC-BAD] old-image-alive=");
    putdec((uint32_t)ok);
    puts("\n\r");
    exit(ok ? 0 : 1);
}
