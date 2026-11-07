#include "lib.h"

static void line(const char* label, uint32_t value) {
    puts("[FD] ");
    puts(label);
    puts("=");
    putdec(value);
    putc('\n');
}

__attribute__((section(".text.start"))) void _start(void) {
    mkdir("/fdhome");
    line("chdir", (uint32_t)chdir("/fdhome"));

    char cwd[64];
    char* got = getcwd(cwd, sizeof(cwd));
    line("getcwd", got != 0);
    puts("[FD] cwd=");
    puts(got ? cwd : "?");
    putc('\n');

    int fd = open("note.txt", O_CREAT | O_RDWR | O_TRUNC);
    line("open-create", fd >= 3);
    int wrote = fd >= 0 ? write(fd, "kernel", 6) : -1;
    line("write-first", wrote == 6);
    line("lseek-end", lseek(fd, 0, SEEK_END) == 6);
    wrote = fd >= 0 ? write(fd, " fd", 3) : -1;
    line("write-second", wrote == 3);
    line("lseek-start", lseek(fd, 0, SEEK_SET) == 0);

    char buf[32];
    int gotn = fd >= 0 ? read(fd, buf, 6) : -1;
    line("read-first", gotn == 6);
    line("content-first", gotn == 6 &&
         buf[0] == 'k' && buf[1] == 'e' && buf[2] == 'r' &&
         buf[3] == 'n' && buf[4] == 'e' && buf[5] == 'l');
    line("read-badptr", read(fd, (void*)0xB8000, 1) == -1);
    line("close", close(fd) == 0);
    line("close-badfd", close(fd) == -1);

    fd = open("note.txt", O_RDONLY);
    line("open-read", fd >= 3);
    gotn = fd >= 0 ? read(fd, buf, sizeof(buf) - 1) : -1;
    if (gotn > 0) buf[gotn] = '\0';
    line("read-all", gotn == 9);
    puts("[FD] content=");
    puts(gotn > 0 ? buf : "?");
    putc('\n');
    close(fd);

    int pid = fork();
    if (pid == 0) {
        int ok = chdir("/") == 0;
        ok = ok && getcwd(cwd, sizeof(cwd)) && cwd[0] == '/' && cwd[1] == '\0';
        ok = ok && open("note.txt", O_RDONLY) < 0;
        line("child-cwd-isolated", (uint32_t)ok);
        exit(7);
    }

    int status = -1;
    int reaped = waitpid(-1, &status);
    line("child-reaped", reaped == pid && status == 7);
    got = getcwd(cwd, sizeof(cwd));
    line("parent-cwd-kept", got && cwd[0] == '/' &&
         cwd[1] == 'f' && cwd[2] == 'd' && cwd[3] == 'h' &&
         cwd[4] == 'o' && cwd[5] == 'm' && cwd[6] == 'e' && cwd[7] == '\0');

    fd = open("note.txt", O_RDONLY);
    line("relative-after-fork", fd >= 3);
    if (fd >= 0) close(fd);

    const char* passed = "fd-system-pass\n";
    line("stdout-fd", write(1, passed, 15) == 15);
    line("stderr-fd", write(2, passed, 15) == 15);

    int ok = fd >= 0 && wrote == 3 && gotn == 9 && got && pid > 0 &&
             reaped == pid && status == 7;
    line("done", (uint32_t)ok);
    exit(ok ? 0 : 1);
}
