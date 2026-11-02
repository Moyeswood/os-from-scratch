#ifndef USER_H
#define USER_H

#include "types.h"

// ============================================================
//  Ring 3 用户态 (里程碑 10)
//
//  架构: 共享地址空间, 内核 0~32MB (supervisor), 用户 32~36MB
//  (USER 页, 见 paging.c)。用户程序链接在 USER_BASE, 栈顶
//  USER_BASE+USER_SIZE。用户进程跑在 shell 的键盘中断处理链
//  里 (和游戏同款架构), 期间 IRQ0 照常滴答但调度器跳过抢占。
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

// run 命令入口: 把内嵌镜像拷进用户区, iret 进 Ring 3;
// 进程 exit 或被异常杀掉后从这里返回到 shell
void user_run(const char* name);

// 异常处理调用: 用户态异常 → 杀掉当前用户进程 (longjmp 回 user_run)
void user_fault_kill(void);

// syscall 帧 (user_asm.asm pusha 顺序 + iret 返回帧):
//   [0]=edi [1]=esi [2]=ebp [3]=esp(进入时) [4]=ebx [5]=edx [6]=ecx [7]=eax
//   [8]=eip [9]=cs [10]=eflags [11]=esp(用户态) [12]=ss(用户态)
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

#endif
