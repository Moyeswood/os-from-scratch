#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    const char* ready = "[FD] stdin-ready\n";
    write(1, ready, 17);

    char buf[32];
    int n = read(0, buf, sizeof(buf) - 1);
    if (n > 0) buf[n] = '\0';

    const char* prefix = "[FD] stdin=";
    write(1, prefix, 11);
    if (n > 0) write(1, buf, (uint32_t)n);
    write(1, "\n", 1);

    int ok = n == 6 && buf[0] == 'h' && buf[1] == 'e' &&
             buf[2] == 'l' && buf[3] == 'l' && buf[4] == 'o' &&
             buf[5] == '\n';
    const char* result = ok ? "[FD] stdin-pass\n" : "[FD] stdin-fail\n";
    write(1, result, ok ? 16 : 16);
    exit(ok ? 0 : 1);
}
