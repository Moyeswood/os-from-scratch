#include "lib.h"

static void line(const char* name, int value) {
    puts("[WAIT] ");
    puts(name);
    puts("=");
    putdec((uint32_t)value);
    puts("\n\r");
}

__attribute__((section(".text.start"))) void _start(void) {
    int first = fork();
    if (first == 0) exit(42);

    sleep(20);   // 留窗口给后台 spawn 清扫，验证活父进程的僵尸不会被偷走
    int status = -1;
    int reaped = waitpid(first, &status);
    line("specific-pid", reaped);
    line("specific-status", status);
    int specific_status = status;

    int second = fork();
    if (second == 0) exit(7);

    int any_status = -1;
    int any = waitpid(-1, &any_status);
    line("any-pid", any);
    line("any-status", any_status);

    status = 99;
    int none = waitpid(-1, &status);
    line("no-child-ok", none == -1);
    line("status-untouched", status);
    int badptr = waitpid(-1, (int*)0xB8000);
    line("badptr-ok", badptr == -1);

    int ok = first > 0 && reaped == first && specific_status == 42 &&
             second > 0 && any == second && any_status == 7 &&
             none == -1 && status == 99 && badptr == -1;
    line("done", ok);
    exit(ok ? 0 : 1);
}
