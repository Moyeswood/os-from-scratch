#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    puts("[EXEC-DEMO] before pid=");
    putdec((uint32_t)getpid());
    puts("\n\r");
    puts("[EXEC-DEMO] replacing image with /bin/hello\n\r");
    int r = exec("/bin/hello");
    puts("[EXEC-DEMO] BUG: exec returned ");
    putdec((uint32_t)r);
    puts("\n\r");
    exit(1);
}
