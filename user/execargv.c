#include "lib.h"

__attribute__((section(".text.start"))) void _start(void) {
    char* argv[] = { "/bin/elfdemo", "from-exec", "env-test", 0 };
    char* envp[] = { "ORIGIN=exec", 0 };
    puts("[EXEC-ARGV] replacing with /bin/elfdemo\n\r");
    int r = execve("/bin/elfdemo", argv, envp);
    puts("[EXEC-ARGV] BUG return=");
    putdec((uint32_t)r);
    puts("\n\r");
    exit(1);
}
