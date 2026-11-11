#include "lib.h"

#define BIG_SIZE (25u * 4096u)
#define STRESS_CHUNKS 4u

static uint8_t* block;

static void fill_block(uint32_t seed) {
    for (uint32_t i = 0; i < 4096; ++i)
        block[i] = (uint8_t)((i * 13u + seed * 29u) & 0xFF);
}

/* 把 v 的 8 位十六进制写入 line[pos..], 更新 pos (诊断用) */
static void hex8(char* line, int* pos, uint32_t v) {
    const char digits[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4)
        line[(*pos)++] = digits[(v >> shift) & 0xF];
}

static int write_all(int fd, const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t done = 0;
    while (done < len) {
        int n = write(fd, p + done, len - done);
        if (n <= 0) return -1;
        done += (uint32_t)n;
    }
    return 0;
}

static int write_file(const char* path, uint32_t chunks, uint32_t seed) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0) return -1;
    int ok = 1;
    for (uint32_t c = 0; c < chunks; ++c) {
        fill_block(seed + c);
        if (write_all(fd, block, 4096) < 0) {
            ok = 0;
            break;
        }
    }
    if (close(fd) < 0) ok = 0;
    return ok ? 0 : -1;
}

static int verify_file(const char* path, uint32_t chunks, uint32_t seed) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int ok = 1;
    for (uint32_t c = 0; c < chunks; ++c) {
        uint32_t done = 0;
        while (done < 4096) {
            int n = read(fd, block + done, 4096 - done);
            if (n <= 0) {
                if (ok) {
                    char line[80];
                    int pos = 0;
                    const char p1[] = "[FS2] verify-shortread chunk=0x";
                    for (int k = 0; p1[k]; ++k) line[pos++] = p1[k];
                    hex8(line, &pos, c);
                    const char p2[] = " got=0x";
                    for (int k = 0; p2[k]; ++k) line[pos++] = p2[k];
                    hex8(line, &pos, done);
                    const char p3[] = " n=";
                    for (int k = 0; p3[k]; ++k) line[pos++] = p3[k];
                    line[pos++] = n < 0 ? 'E' : '0';
                    line[pos++] = '\n';
                    line[pos] = '\0';
                    puts(line);
                }
                ok = 0;
                break;
            }
            done += (uint32_t)n;
        }
        if (!ok) break;
        /* 修复(2026-11-11): 旧代码在此先 fill_block 再生预期模式覆写刚读回的数据,
         * 再与公式比较 —— 预期 vs 预期恒真, 内容校验形同虚设。
         * 正确做法: 直接拿读回的数据与期望公式比较, 不碰 block。 */
        for (uint32_t i = 0; i < 4096; ++i)
            if (block[i] != (uint8_t)((i * 13u + (seed + c) * 29u) & 0xFF)) {
                if (ok) {
                    /* 诊断: 打印首个不匹配位置与期望/实际字节 */
                    char line[96];
                    int pos = 0;
                    const char p1[] = "[FS2] verify-mismatch chunk=0x";
                    for (int k = 0; p1[k]; ++k) line[pos++] = p1[k];
                    hex8(line, &pos, c);
                    const char p2[] = " off=0x";
                    for (int k = 0; p2[k]; ++k) line[pos++] = p2[k];
                    hex8(line, &pos, i);
                    const char p3[] = " want=0x";
                    for (int k = 0; p3[k]; ++k) line[pos++] = p3[k];
                    hex8(line, &pos,
                         (uint32_t)(uint8_t)((i * 13u + (seed + c) * 29u) & 0xFF));
                    const char p4[] = " got=0x";
                    for (int k = 0; p4[k]; ++k) line[pos++] = p4[k];
                    hex8(line, &pos, (uint32_t)block[i]);
                    line[pos++] = '\n';
                    line[pos] = '\0';
                    puts(line);
                }
                ok = 0;
                break;
            }
        if (!ok) break;
    }
    int extra = read(fd, block, 4096);
    if (extra != 0) ok = 0;
    close(fd);
    return ok ? 0 : -1;
}

static void stress_path(char* out, int child, int file) {
    const char prefix[] = "/fscheck/stress-";
    int pos = 0;
    for (int i = 0; prefix[i]; ++i) out[pos++] = prefix[i];
    out[pos++] = (char)('0' + child);
    out[pos++] = '-';
    out[pos++] = (char)('0' + file);
    const char tail[] = ".bin";
    for (int i = 0; tail[i]; ++i) out[pos++] = tail[i];
    out[pos] = '\0';
}

static int stress_child(int child) {
    char path[64];
    // Both children are alive before either starts creating files, so the
    // stress run exercises interleaved public FS operations, not serial runs.
    sleep(2);
    for (int file = 0; file < 3; ++file) {
        stress_path(path, child, file);
        if (write_file(path, STRESS_CHUNKS, (uint32_t)(child * 3 + file)) < 0)
            return 1;
    }
    return 0;
}

static int run_create(void) {
    mkdir("/fscheck");
    int ok = write_file("/fscheck/big.bin", BIG_SIZE / 4096u, 7) == 0;
    puts(ok ? "[FS2] big-write=1\n" : "[FS2] big-write=0\n");
    ok = ok && verify_file("/fscheck/big.bin", BIG_SIZE / 4096u, 7) == 0;
    puts(ok ? "[FS2] big-verify=1\n" : "[FS2] big-verify=0\n");

    /* 负向自检: 故意损坏一个字节, 校验必须失败 —— 证明内容校验真实有效
     * (2026-11-11 修复 verify_file 恒真 bug 后补上的防回归测试) */
    {
        int corrupted = write_file("/fscheck/corrupt.bin", 1, 9) == 0;
        int fd = open("/fscheck/corrupt.bin", O_WRONLY);
        uint8_t bad = 0xAA;
        if (fd >= 0) {
            if (write(fd, &bad, 1) != 1) corrupted = 0;
            close(fd);
        } else {
            corrupted = 0;
        }
        /* seed=9 时 offset 0 的期望字节是 5, 写 0xAA 后校验必须报错 */
        corrupted = corrupted && verify_file("/fscheck/corrupt.bin", 1, 9) < 0;
        puts(corrupted ? "[FS2] corruption-detected=1\n"
                       : "[FS2] corruption-detected=0\n");
        ok = ok && corrupted;
    }

    int pids[2] = { -1, -1 };
    for (int child = 0; child < 2; ++child) {
        pids[child] = fork();
        if (pids[child] == 0) exit(stress_child(child));
        if (pids[child] < 0) ok = 0;
    }
    sleep(3);
    for (int child = 0; child < 2; ++child) {
        if (pids[child] <= 0) continue;
        int status = -1;
        if (waitpid(pids[child], &status) != pids[child] ||
            WEXITSTATUS(status) != 0)
            ok = 0;
    }
    puts(ok ? "[FS2] concurrent-write=1\n"
            : "[FS2] concurrent-write=0\n");

    char path[64];
    for (int child = 0; child < 2; ++child) {
        for (int file = 0; file < 3; ++file) {
            stress_path(path, child, file);
            if (verify_file(path, STRESS_CHUNKS,
                            (uint32_t)(child * 3 + file)) < 0)
                ok = 0;
        }
    }
    puts(ok ? "[FS2] concurrent-verify=1\n"
            : "[FS2] concurrent-verify=0\n");

    user_stat_t info;
    int renamed = rename("/fscheck/big.bin", "/fscheck/moved.bin") == 0 &&
                  stat("/fscheck/moved.bin", &info) == 0 &&
                  info.type == 1 && info.size == BIG_SIZE &&
                  stat("/fscheck/big.bin", &info) < 0;
    puts(renamed ? "[FS2] rename-stat=1\n" : "[FS2] rename-stat=0\n");
    ok = ok && renamed;
    puts(ok ? "[FS2] done=1\n" : "[FS2] done=0\n");
    return ok ? 0 : 1;
}

static int run_verify(void) {
    user_stat_t info;
    int ok = stat("/fscheck/renamed.bin", &info) == 0 &&
             info.type == 1 && info.size == BIG_SIZE;
    puts(ok ? "[FS2] reboot-stat=1\n" : "[FS2] reboot-stat=0\n");
    ok = ok && verify_file("/fscheck/renamed.bin", BIG_SIZE / 4096u, 7) == 0;
    puts(ok ? "[FS2] reboot-verify=1\n" : "[FS2] reboot-verify=0\n");
    return ok ? 0 : 1;
}

__attribute__((section(".text.start")))
void _start(int argc, char** argv, char** envp) {
    (void)envp;
    block = (uint8_t*)malloc(4096);
    if (!block) {
        puts("[FS2] allocation=0\n");
        exit(1);
    }
    int status;
    if (argc > 1 && argv[1][0] == 'v' && argv[1][1] == 'e')
        status = run_verify();
    else
        status = run_create();
    exit(status);
}
