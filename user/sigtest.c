#include "lib.h"

static volatile int handled;
static volatile int restart_handled;

static void on_signal(int sig) {
    handled++;
    if (sig == SIGINT) puts("[SIGTEST] handler-int=1\n");
    if (sig == SIGTERM) puts("[SIGTEST] handler-term=1\n");
}

static void on_restart(int sig) {
    (void)sig;
    restart_handled++;
    puts("[SIGTEST] restart-handler=1\n");
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

    int restart_ok = 0;
    if (signal_flags(SIGINT, on_restart, SA_RESTART) ==
        (sighandler_t)on_signal) {
        puts("[SIGTEST] install-restart=1\n");
        int parent = getpid();
        int pid = fork();
        if (pid == 0) {
            sleep(20);
            kill(parent, SIGINT);
            sleep(20);
            exit(7);
        }
        int status = 0;
        int waited = pid > 0 ? waitpid(pid, &status) : -1;
        if (waited == pid && WEXITSTATUS(status) == 7)
            restart_ok = 1;
    }
    puts(restart_handled == 1 && restart_ok ?
         "[SIGTEST] restart-resumed=1\n" :
         "[SIGTEST] restart-resumed=0\n");

    int pipe_restart_ok = 0;
    int fds[2];
    if (pipe(fds) == 0) {
        int parent = getpid();
        int pid = fork();
        if (pid == 0) {
            close(fds[0]);
            sleep(20);
            kill(parent, SIGINT);
            write(fds[1], "R", 1);
            exit(0);
        }
        close(fds[1]);
        char c = 0;
        int got = pid > 0 ? read(fds[0], &c, 1) : -1;
        close(fds[0]);
        int status = 0;
        if (pid > 0) waitpid(pid, &status);
        pipe_restart_ok = got == 1 && c == 'R' && status == 0;
    }
    puts(restart_handled == 2 && pipe_restart_ok ?
         "[SIGTEST] pipe-restart=1\n" :
         "[SIGTEST] pipe-restart=0\n");

    int eintr_ok = 0;
    if (signal(SIGINT, on_signal) == (sighandler_t)on_restart &&
        pipe(fds) == 0) {
        int parent = getpid();
        int pid = fork();
        if (pid == 0) {
            close(fds[0]);
            sleep(20);
            kill(parent, SIGINT);
            exit(0);
        }
        close(fds[1]);
        char c = 0;
        int got = pid > 0 ? read(fds[0], &c, 1) : 0;
        close(fds[0]);
        int status = 0;
        if (pid > 0) waitpid(pid, &status);
        eintr_ok = got == -1 && status == 0;
    }
    puts(eintr_ok ? "[SIGTEST] eintr=1\n" : "[SIGTEST] eintr=0\n");

    int ok = handled == 4 && restart_ok && pipe_restart_ok && eintr_ok;
    puts(ok ? "[SIGTEST] done=1\n" : "[SIGTEST] done=0\n");
    exit(ok ? 0 : 1);
}
