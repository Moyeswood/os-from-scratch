#include "lib.h"

static int passed;

static void check(const char* name, int ok) {
    puts("[ERR] ");
    puts(name);
    puts("=");
    putdec(ok ? 1 : 0);
    puts(" errno=");
    putdec((uint32_t)errno);
    puts("\n");
    if (!ok) passed = 0;
}

__attribute__((section(".text.start"))) void _start(void) {
    passed = 1;
    char byte = 0;
    user_stat_t info;
    user_dirent_t entry;

    errno = 0;
    check("missing-open", open("/missing", O_RDONLY) == -1 &&
                          errno == ENOENT);

    errno = 0;
    check("directory-open", open("/", O_RDONLY) == -1 &&
                            errno == EISDIR);

    errno = 0;
    check("bad-close", close(99) == -1 && errno == EBADF);

    errno = 0;
    check("bad-read-fd", read(99, &byte, 1) == -1 && errno == EBADF);

    errno = 0;
    check("missing-chdir", chdir("/missing") == -1 && errno == ENOENT);

    errno = 0;
    check("mkdir-create", mkdir("/errcase") == 0);
    errno = 0;
    check("mkdir-exists", mkdir("/errcase") == -1 && errno == EEXIST);

    int fd = open("/errcase/file", O_CREAT | O_RDWR | O_TRUNC);
    check("file-create", fd >= 3);
    check("file-write", fd >= 3 && write(fd, "x", 1) == 1);
    errno = 0;
    check("file-not-dir", readdir("/errcase/file", 0, &entry) == -1 &&
                          errno == ENOTDIR);

    errno = 0;
    check("bad-read-pointer", fd >= 3 &&
          read(fd, (void*)0xB8000, 1) == -1 && errno == EFAULT);

    errno = 0;
    check("missing-stat", stat("/missing", &info) == -1 && errno == ENOENT);
    errno = 0;
    check("bad-stat-pointer",
          stat("/errcase/file", (user_stat_t*)0xB8000) == -1 &&
          errno == EFAULT);

    char tiny[1];
    errno = 0;
    check("cwd-range", getcwd(tiny, sizeof(tiny)) == 0 &&
                       errno == ERANGE);

    int pipefd[2];
    check("pipe-create", pipe(pipefd) == 0);
    close(pipefd[0]);
    errno = 0;
    check("pipe-no-reader", write(pipefd[1], "x", 1) == -1 &&
                            errno == EPIPE);
    close(pipefd[1]);

    errno = 0;
    check("missing-exec", exec("/missing-exec") == -1 &&
                          errno == ENOENT);
    errno = 0;
    check("missing-kill", kill(9999, SIGTERM) == -1 && errno == ESRCH);
    int status = 0;
    errno = 0;
    check("missing-child", waitpid(9999, &status) == -1 &&
                           errno == ECHILD);

    if (fd >= 0) close(fd);
    remove("/errcase/file");
    remove("/errcase");
    puts("[ERR] done=");
    putdec(passed);
    puts("\n");
    exit(passed ? 0 : 1);
}
