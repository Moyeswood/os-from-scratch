#include "lib.h"

static volatile int handled;

static void on_signal(int sig) {
    handled++;
    if (sig == SIGINT) puts("[SIGTEST] handler-int=1\n");
    if (sig == SIGTERM) puts("[SIGTEST] handler-term=1\n");
}

__attribute__((section(".text.start")))
void _start(int argc, char** argv, char** envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    if (signal(SIGINT, on_signal) == (sighandler_t)SIG_DFL)
        puts("[SIGTEST] install-int=1\n");
    if (kill(getpid(), SIGINT) == 0 && handled == 1)
        puts("[SIGTEST] self-signal=1\n");

    if (signal(SIGTERM, on_signal) == (sighandler_t)SIG_DFL)
        puts("[SIGTEST] install-term=1\n");
    uint32_t set = 1u << (SIGTERM - 1);
    uint32_t old = 0;
    if (sigprocmask(SIG_BLOCK, &set, &old) == 0 &&
        kill(getpid(), SIGTERM) == 0 && handled == 1)
        puts("[SIGTEST] masked-pending=1\n");
    if (sigprocmask(SIG_UNBLOCK, &set, 0) == 0 && handled == 2)
        puts("[SIGTEST] unmask-deliver=1\n");

    if (signal(SIGTERM, (sighandler_t)SIG_IGN) == (sighandler_t)on_signal) {
        kill(getpid(), SIGTERM);
        if (handled == 2) puts("[SIGTEST] ignore=1\n");
    }

    signal(SIGINT, on_signal);
    kill(getpid(), SIGINT);
    puts("[SIGTEST] returned=1\n");
    puts(handled == 3 ? "[SIGTEST] done=1\n" : "[SIGTEST] done=0\n");
    exit(handled == 3 ? 0 : 1);
}
