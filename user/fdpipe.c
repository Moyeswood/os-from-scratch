#include "lib.h"

static int pass;

static void check(const char* name, int ok) {
    puts("[PIPE] ");
    puts(name);
    puts("=");
    putdec((uint32_t)(ok ? 1 : 0));
    puts("\n");
    if (!ok) pass = 0;
}

__attribute__((section(".text.start"))) void _start(void) {
    pass = 1;
    mkdir("/pipehome");
    chdir("/pipehome");

    int fd = open("shared.txt", O_CREAT | O_RDWR | O_TRUNC);
    int wrote = fd >= 0 ? write(fd, "abcdef", 6) : -1;
    lseek(fd, 0, SEEK_SET);

    int pid = fork();
    if (pid == 0) {
        char first[4] = { 0 };
        int n = read(fd, first, 3);
        exit(n == 3 && first[0] == 'a' && first[1] == 'b' &&
             first[2] == 'c' ? 0 : 1);
    }
    int status = -1;
    check("child-read", wrote == 6 && waitpid(pid, &status) == pid &&
                       status == 0);

    char rest[4] = { 0 };
    int n = read(fd, rest, 3);
    check("shared-offset", n == 3 && rest[0] == 'd' &&
                           rest[1] == 'e' && rest[2] == 'f');
    close(fd);

    int p[2];
    check("pipe-create", pipe(p) == 0);
    pid = fork();
    if (pid == 0) {
        close(p[0]);
        int ok = write(p[1], "pipe-data", 9) == 9;
        close(p[1]);
        exit(ok ? 0 : 1);
    }
    close(p[1]);
    char buf[32];
    int total = 0;
    while (total < (int)sizeof(buf)) {
        n = read(p[0], buf + total, sizeof(buf) - (uint32_t)total);
        if (n <= 0) break;
        total += n;
    }
    close(p[0]);
    status = -1;
    check("pipe-data", waitpid(pid, &status) == pid && status == 0 &&
                       total == 9 && buf[0] == 'p' && buf[8] == 'a');

    check("pipe-create-2", pipe(p) == 0);
    pid = fork();
    if (pid == 0) {
        close(p[0]);
        int ok = dup2(p[1], 1) == 1;
        close(p[1]);
        ok = ok && write(1, "dup", 3) == 3;
        close(1);
        exit(ok ? 0 : 1);
    }
    close(p[1]);
    char dupbuf[4] = { 0 };
    n = read(p[0], dupbuf, 3);
    close(p[0]);
    status = -1;
    check("dup2-stdout", waitpid(pid, &status) == pid && status == 0 &&
                         n == 3 && dupbuf[0] == 'd' &&
                         dupbuf[1] == 'u' && dupbuf[2] == 'p');

    check("done", pass);
    exit(pass ? 0 : 1);
}
