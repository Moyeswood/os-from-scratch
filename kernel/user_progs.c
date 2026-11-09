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
extern const char user_memdemo_start[], user_memdemo_end[];
extern const char user_allocdemo_start[], user_allocdemo_end[];
extern const char user_filedemo_start[], user_filedemo_end[];
extern const char user_fdstdin_start[], user_fdstdin_end[];
extern const char user_vmtest_start[], user_vmtest_end[];
extern const char user_vmguard_start[], user_vmguard_end[];
extern const char user_execdemo_start[], user_execdemo_end[];
extern const char user_execbad_start[], user_execbad_end[];
extern const char user_waittest_start[], user_waittest_end[];
extern const char user_execargv_start[], user_execargv_end[];
extern const char user_sh_start[], user_sh_end[];
extern const char user_fdpipe_start[], user_fdpipe_end[];
extern const char user_fscheck_start[], user_fscheck_end[];
extern const char user_sigtest_start[], user_sigtest_end[];
extern const char user_errtest_start[], user_errtest_end[];

static user_prog_t progs[23];
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

    progs[8].name = "vmtest";
    progs[8].data = (const uint8_t*)user_vmtest_start;
    progs[8].size = (uint32_t)(user_vmtest_end - user_vmtest_start);

    progs[9].name = "vmguard";
    progs[9].data = (const uint8_t*)user_vmguard_start;
    progs[9].size = (uint32_t)(user_vmguard_end - user_vmguard_start);

    progs[10].name = "execdemo";
    progs[10].data = (const uint8_t*)user_execdemo_start;
    progs[10].size = (uint32_t)(user_execdemo_end - user_execdemo_start);

    progs[11].name = "execbad";
    progs[11].data = (const uint8_t*)user_execbad_start;
    progs[11].size = (uint32_t)(user_execbad_end - user_execbad_start);

    progs[12].name = "waittest";
    progs[12].data = (const uint8_t*)user_waittest_start;
    progs[12].size = (uint32_t)(user_waittest_end - user_waittest_start);

    progs[13].name = "execargv";
    progs[13].data = (const uint8_t*)user_execargv_start;
    progs[13].size = (uint32_t)(user_execargv_end - user_execargv_start);

    progs[14].name = "memdemo";
    progs[14].data = (const uint8_t*)user_memdemo_start;
    progs[14].size = (uint32_t)(user_memdemo_end - user_memdemo_start);

    progs[15].name = "allocdemo";
    progs[15].data = (const uint8_t*)user_allocdemo_start;
    progs[15].size = (uint32_t)(user_allocdemo_end - user_allocdemo_start);

    progs[16].name = "filedemo";
    progs[16].data = (const uint8_t*)user_filedemo_start;
    progs[16].size = (uint32_t)(user_filedemo_end - user_filedemo_start);

    progs[17].name = "fdstdin";
    progs[17].data = (const uint8_t*)user_fdstdin_start;
    progs[17].size = (uint32_t)(user_fdstdin_end - user_fdstdin_start);

    progs[18].name = "sh";
    progs[18].data = (const uint8_t*)user_sh_start;
    progs[18].size = (uint32_t)(user_sh_end - user_sh_start);

    progs[19].name = "fdpipe";
    progs[19].data = (const uint8_t*)user_fdpipe_start;
    progs[19].size = (uint32_t)(user_fdpipe_end - user_fdpipe_start);

    progs[20].name = "fscheck";
    progs[20].data = (const uint8_t*)user_fscheck_start;
    progs[20].size = (uint32_t)(user_fscheck_end - user_fscheck_start);

    progs[21].name = "sigtest";
    progs[21].data = (const uint8_t*)user_sigtest_start;
    progs[21].size = (uint32_t)(user_sigtest_end - user_sigtest_start);

    progs[22].name = "errtest";
    progs[22].data = (const uint8_t*)user_errtest_start;
    progs[22].size = (uint32_t)(user_errtest_end - user_errtest_start);
}
