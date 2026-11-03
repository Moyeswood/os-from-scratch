#ifndef USER_H
#define USER_H

#include "types.h"

// ============================================================
//  Ring 3 用户态 (里程碑 10) + 多进程 (里程碑 11b)
//
//  用户程序链接在 USER_BASE (32MB), 栈顶 USER_BASE+USER_SIZE。
//  里程碑 11b 起每个用户进程 = 一个调度任务: 独立页目录
//  (虚拟 32~36MB → 自己的一整块物理内存) + 独立内核栈
//  (TSS.esp0)。进程管理在 proc.c, 本文件保留: 程序表、
//  系统调用号、int 0x80 帧布局。
// ============================================================

// 用户程序描述: 名字 → 内嵌镜像 (user_embed.asm incbin, 见 user_progs.c)
typedef struct {
    const char* name;
    const uint8_t* data;
    uint32_t size;
} user_prog_t;

extern const user_prog_t* user_progs;   // 表 (user_progs.c, 运行时填充)
extern const int user_prog_count;

void user_progs_init(void);   // 运行时填程序表 (blob 大小是链接期常量)

// 内核初始化: 保留用户区物理页 + 映射 USER 页表
void user_init();

// run 命令入口 (shell.c 调): 建进程; 前台 = 等它退出, 后台 = 立刻返回
void user_run(const char* name, int background);

// syscall 帧 (user_asm.asm pusha 顺序 + iret 返回帧):
//   [0]=edi [1]=esi [2]=ebp [3]=esp(进入时) [4]=ebx [5]=edx [6]=ecx [7]=eax
//   [8]=eip [9]=cs [10]=eflags [11]=esp(用户态) [12]=ss(用户态)
//   (里程碑 11b: 这个布局恰好 = 用户任务的 52 字节上下文帧,
//    fork 直接把它抄到子进程内核栈, 改 eax=0 即可)
typedef struct {
    uint32_t edi, esi, ebp, esp_orig, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags, esp_user, ss_user;
} user_frame_t;

// 系统调用号 (与 user/lib.h 的 SYS_* 一致)
#define SYS_EXIT     0
#define SYS_PUTC     1
#define SYS_PUTS     2
#define SYS_GETKEY   3
#define SYS_GETTICKS 4
#define SYS_FORK     5   // 里程碑 11b: 复制地址空间 + 现场, 父得子 pid, 子得 0
#define SYS_GETPID   6   // 里程碑 11b: 读自己的 pid
#define SYS_SLEEP    7   // 里程碑 11b: 睡 ebx 个滴答

#endif
