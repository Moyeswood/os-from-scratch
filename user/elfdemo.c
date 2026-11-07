#include "lib.h"

static volatile int initialized = 7;
static volatile unsigned char bss_area[8192];

static void line(const char* name, uint32_t value) {
    puts("[ELF-DEMO] ");
    puts(name);
    puts("=");
    putdec(value);
    puts("\n\r");
}

__attribute__((section(".text.start")))
void _start(int argc, char** argv, char** envp) {
    line("argc", (uint32_t)argc);
    line("data", (uint32_t)initialized);
    if (argc > 1) {
        puts("[ELF-DEMO] argv1=");
        puts(argv[1]);
        puts("\n\r");
    }
    if (argc > 2) {
        puts("[ELF-DEMO] argv2=");
        puts(argv[2]);
        puts("\n\r");
    }
    if (envp && envp[0]) {
        puts("[ELF-DEMO] env0=");
        puts(envp[0]);
        puts("\n\r");
    }

    uint32_t sum = 0;
    for (uint32_t i = 0; i < sizeof(bss_area); ++i) sum += bss_area[i];
    line("bss-zero", sum == 0);

    int ok = initialized == 7 && sum == 0 && argc > 0 && argv &&
             argv[0] && envp;
    line("done", (uint32_t)ok);
    exit(ok ? 0 : 1);
}
