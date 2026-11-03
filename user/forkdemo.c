// ============================================================
//  用户程序: forkdemo — fork + 独立地址空间演示 (run forkdemo)
//
//  流程: fork() 之后父子各走各的 —
//    - 子进程把全局变量 counter 改成 200
//    - 父进程随后打印自己的 counter, 必须是 100
//  同一份代码、同一个变量名, 各自的内存: 这就是每进程
//  独立页目录的意义 (写自己的页, 别人的页动不了)。
//  两个进程同时打印, 靠 sleep 让出节奏, 测试断言输出顺序。
// ============================================================
#include "lib.h"

int counter = 100;   // 全局变量: 父子各自一份 (独立地址空间)

__attribute__((section(".text.start"))) void _start(void) {
    int pid = fork();
    if (pid == 0) {
        // ---- 子进程 ----
        puts("child: pid="); putdec((uint32_t)getpid());
        puts(", counter="); putdec((uint32_t)counter); puts("\n\r");
        counter = 200;                       // 改"自己的"副本
        puts("child: set counter=200 (my own copy)\n\r");
        sleep(2);                            // 让父进程先看到这行
        for (int i = 0; i < 4; i++) {
            puts("child: counter="); putdec((uint32_t)counter); puts("\n\r");
            counter++;
            sleep(4);
        }
        puts("child: exiting\n\r");
        exit(0);
    } else if (pid > 0) {
        // ---- 父进程 ----
        puts("parent: forked pid="); putdec((uint32_t)pid);
        puts(", my pid="); putdec((uint32_t)getpid()); puts("\n\r");
        sleep(2);                            // 确保子进程先改完 200
        for (int i = 0; i < 4; i++) {
            puts("parent: counter="); putdec((uint32_t)counter); puts("\n\r");
            counter++;
            sleep(4);
        }
        puts("parent: exiting\n\r");
        exit(0);
    } else {
        puts("forkdemo: fork failed\n\r");
        exit(1);
    }
}
