#ifndef USER_H
#define USER_H

#include "types.h"

// ============================================================
//  Ring 3 用户态 (里程碑 10) + 多进程 (里程碑 11b)
//
//  用户程序链接在 USER_BASE (32MB), 栈顶 USER_BASE+USER_SIZE。
//  每个用户进程 = 一个调度任务: 独立页目录 + 按需分配的物理页
//  + 独立内核栈 (TSS.esp0)。进程/Virtual Memory 管理分别在
//  proc.c/vm.c, 本文件保留: 程序表、系统调用号、int 0x80 帧布局。
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

// 内核初始化: 初始化内嵌程序表并报告每进程 VM 布局
void user_init();

// run 命令入口 (shell.c 调): 建进程; 前台 = 等它退出, 后台 = 立刻返回
void user_run(const char* name, int argc, char** argv, int background);

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
#define SYS_BRK      8   // 里程碑 13: 设置堆断点, 返回旧断点
#define SYS_MMAP     9   // 里程碑 13: 预留匿名映射, 返回用户虚拟地址
#define SYS_EXEC     10  // 里程碑 14: 用文件系统镜像替换当前进程
#define SYS_WAITPID  11  // 里程碑 15: 父进程等待并回收直接子进程
#define SYS_OPEN     12  // 里程碑 19: 打开/创建文件, 返回进程级 fd
#define SYS_READ     13  // 里程碑 19: fd 读取
#define SYS_WRITE    14  // 里程碑 19: fd 写入
#define SYS_CLOSE    15  // 里程碑 19: 关闭 fd
#define SYS_LSEEK    16  // 里程碑 19: 调整文件偏移
#define SYS_CHDIR    17  // 里程碑 19: 修改当前进程工作目录
#define SYS_GETCWD   18  // 里程碑 19: 读取当前工作目录
#define SYS_MKDIR    19  // 里程碑 19: 创建目录
#define SYS_PIPE     20  // 里程碑 20: 创建共享读/写端
#define SYS_DUP      21  // 里程碑 20: 复制 fd
#define SYS_DUP2     22  // 里程碑 20: 重定向 fd
#define SYS_REMOVE   23  // 里程碑 20: 删除文件/空目录
#define SYS_READDIR  24  // 里程碑 20: 用户态目录迭代
#define SYS_SETPGID  25  // 里程碑 21: 设置进程组
#define SYS_TCSETPGRP 26 // 里程碑 21: 控制台前台进程组
#define SYS_KILL     27  // 里程碑 21: 向进程或进程组发送终止信号

#define SIGINT   2
#define SIGKILL  9
#define SIGTERM 15

#define O_RDONLY     0x0001
#define O_WRONLY     0x0002
#define O_RDWR       (O_RDONLY | O_WRONLY)
#define O_CREAT      0x0040
#define O_TRUNC      0x0200
#define O_APPEND     0x0400

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// 用户态 readdir 结果（与 user/lib.h 的 user_dirent_t 布局一致）。
typedef struct {
    uint32_t inum;
    uint32_t type;
    uint32_t size;
    char name[28];
} user_dirent_t;

#endif
