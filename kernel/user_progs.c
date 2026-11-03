#include "user.h"

// 用户程序表: 名字 → 内嵌镜像 (user_embed.asm incbin)
// run <name> 按名字查这张表。
// 表在 user_progs_init() 里运行时填充: blob 大小 = end-start 是
// 链接期常量, 不是 C 编译期常量, 不能直接写进初始化器。

extern const char user_hello_start[], user_hello_end[];
extern const char user_count_start[], user_count_end[];
extern const char user_crash_start[], user_crash_end[];
extern const char user_div0_start[], user_div0_end[];
extern const char user_echo_start[], user_echo_end[];
extern const char user_badptr_start[], user_badptr_end[];
extern const char user_forkdemo_start[], user_forkdemo_end[];
extern const char user_spinner_start[], user_spinner_end[];

static user_prog_t progs[8];
const user_prog_t* user_progs = progs;
const int user_prog_count = sizeof(progs) / sizeof(progs[0]);

void user_progs_init(void) {
    progs[0].name = "hello";
    progs[0].data = (const uint8_t*)user_hello_start;
    progs[0].size = (uint32_t)(user_hello_end - user_hello_start);

    progs[1].name = "count";
    progs[1].data = (const uint8_t*)user_count_start;
    progs[1].size = (uint32_t)(user_count_end - user_count_start);

    progs[2].name = "crash";
    progs[2].data = (const uint8_t*)user_crash_start;
    progs[2].size = (uint32_t)(user_crash_end - user_crash_start);

    progs[3].name = "div0";
    progs[3].data = (const uint8_t*)user_div0_start;
    progs[3].size = (uint32_t)(user_div0_end - user_div0_start);

    progs[4].name = "echo";
    progs[4].data = (const uint8_t*)user_echo_start;
    progs[4].size = (uint32_t)(user_echo_end - user_echo_start);

    progs[5].name = "badptr";
    progs[5].data = (const uint8_t*)user_badptr_start;
    progs[5].size = (uint32_t)(user_badptr_end - user_badptr_start);

    progs[6].name = "forkdemo";
    progs[6].data = (const uint8_t*)user_forkdemo_start;
    progs[6].size = (uint32_t)(user_forkdemo_end - user_forkdemo_start);

    progs[7].name = "spinner";
    progs[7].data = (const uint8_t*)user_spinner_start;
    progs[7].size = (uint32_t)(user_spinner_end - user_spinner_start);
}
