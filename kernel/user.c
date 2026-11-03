#include "user.h"
#include "paging.h"
#include "memory.h"
#include "serial.h"
#include "vga.h"
#include "timer.h"
#include "keyboard.h"
#include "string.h"
#include "shell.h"

// ============================================================
//  Ring 3 用户态 (里程碑 10) — 进程运行 + 系统调用分发
//
//  用户进程生命周期:
//    user_run()           查表 → 拷镜像到 USER_BASE → setjmp → enter_user
//    (Ring 3 执行)        输出/读键/计时全走 int 0x80
//    exit 系统调用        → longjmp 回 setjmp 点 (退出码)
//    用户态异常 (#PF/#DE) → exc.c → user_fault_kill → 同一条 longjmp
//
//  为什么能用 longjmp: enter_user 把 TSS.esp0 设成"当时"的内核栈,
//  中断/系统调用/异常都落在那条栈上, 栈是连续的, longjmp 一收
//  就回到 shell 命令的执行链里 (cc.c 编译器错误退出同款手法)。
// ============================================================

static void* user_jmp[8];       // setjmp 缓冲区 (cc.c 同款模式)
static int user_active = 0;     // 防御: 没跑用户进程时 fault 不该 kill
static int user_killed = 0;
static uint32_t user_exit_code = 0;

extern void enter_user(uint32_t entry, uint32_t esp_user);  // user_asm.asm

// ---------- 系统调用实现 (内核侧) ----------

// SYS_PUTC: 输出一个字符 (屏幕 + 串口镜像, 测试靠串口当预言机)
static int sys_putc(char c) {
    vga_putc(c);
    serial_putc(c);
    return 0;
}

// SYS_PUTS: 输出字符串。
// ★安全点: 用户传进来的是指针 — 必须先验证它指向用户区,
//   否则恶意程序可以让内核去读任意内核地址 (真实 OS 用
//   copy_from_user 逐页复制, 这里只做首指针范围检查, 教学版)。
static int sys_puts(const char* s) {
    if ((uint32_t)s < USER_BASE || (uint32_t)s >= USER_BASE + USER_SIZE) {
        serial_puts("[SYS] puts: rejected kernel pointer\n\r");
        return -1;
    }
    // 读循环也限在用户区内: 字符串若越过 36MB 边界, 停住返回
    // (否则内核读到未映射页 → 内核态 #PF panic, 用户可借此 DoS)
    const char* region_end = (const char*)(USER_BASE + USER_SIZE);
    while (*s && s < region_end) {
        vga_putc(*s);
        serial_putc(*s);
        s++;
    }
    return 0;
}

// SYS_GETKEY: 读一个键 (阻塞到有按下事件)。
// 【里程碑 11a】从键盘环形缓冲读 (IRQ1 处理器负责把扫描码推进去),
// 不再轮询 8042 — 那会和 IRQ1 处理器抢字节。阻塞 = 当前任务睡到
// 键盘事件 (IRQ1 唤醒, 下一个 IRQ0 切上 CPU, 至多 55ms 延迟)。
static int sys_getkey(void) {
    return kbd_getchar_blocking(1);   // 1 = 用户语义: ESC → 27
}

// SYS_GETTICKS: 读滴答计数 — 证明用户态期间 IRQ0 定时器照常走
static uint32_t sys_getticks(void) {
    return g_ticks;
}

// ---------- int 0x80 分发 ----------
// 调用约定: eax=调用号, ebx=参数1, ecx=参数2, edx=参数3;
// 返回值写回帧的 eax 槽 (popa 时恢复进 EAX)。
void syscall_dispatch(user_frame_t* f) {
    switch (f->eax) {
    case SYS_EXIT:
        user_exit_code = f->ebx;
        user_killed = 0;
        __builtin_longjmp(user_jmp, 1);      // 直接回 user_run 的 setjmp 点
    case SYS_PUTC:
        f->eax = (uint32_t)sys_putc((char)f->ebx);
        break;
    case SYS_PUTS:
        f->eax = (uint32_t)sys_puts((const char*)f->ebx);
        break;
    case SYS_GETKEY:
        f->eax = (uint32_t)sys_getkey();
        break;
    case SYS_GETTICKS:
        f->eax = sys_getticks();
        break;
    default:
        serial_puts("[SYS] unknown syscall #");
        serial_put_hex(f->eax);
        serial_puts("\n\r");
        f->eax = 0xFFFFFFFF;
        break;
    }
}

// ---------- 异常杀进程 ----------
void user_fault_kill(void) {
    if (!user_active) return;                // 防御: 不该发生
    user_killed = 1;
    __builtin_longjmp(user_jmp, 1);
}

// ---------- 内核初始化 ----------
void user_init() {
    // 用户区物理页是"预占"的固定地址 (程序链接在 0x2000000),
    // 先保留, 别让分配器把它们发给 VM 内存池/FS 块
    memory_reserve(USER_BASE, USER_SIZE);
    paging_map_user_region();
    user_progs_init();
    serial_puts("[USER] user region ");
    serial_put_hex(USER_BASE);
    serial_puts("..");
    serial_put_hex(USER_BASE + USER_SIZE);
    serial_puts(" mapped (U/S=1, ring 3)\n\r");
}

// ---------- run 命令入口 ----------
void user_run(const char* name) {
    const user_prog_t* p = 0;
    for (int i = 0; i < user_prog_count; i++) {
        if (k_strcmp(user_progs[i].name, name) == 0) { p = &user_progs[i]; break; }
    }
    if (!p) {
        serial_puts("[USER] unknown program: ");
        serial_puts(name);
        serial_puts("\n\r");
        vga_puts("\n\rUnknown program: ");
        vga_puts(name);
        vga_puts("  (try: run hello | count | crash | div0 | echo | badptr)\n\r");
        return;
    }
    if (p->size > USER_SIZE - 4096) {
        serial_puts("[USER] program too big\n\r");
        vga_puts("\n\rrun: program image too large\n\r");
        return;
    }

    // 把内嵌镜像拷进用户区 (内核写 USER 页合法: CPL0 不看 U/S)
    k_memcpy((void*)USER_BASE, p->data, p->size);

    serial_puts("[USER] run ");
    serial_puts(name);
    serial_puts(" (size=");
    serial_put_hex(p->size);
    serial_puts(", entry=0x02000000, esp=0x02400000)\n\r");

    // exit / 异常杀进程都从这里返回 (值 != 0)
    if (__builtin_setjmp(user_jmp) != 0) {
        user_active = 0;
        // 恢复内核数据段: 用户态里 DS/ES/FS/GS 被 enter_user 换成了
        // 0x23 (DPL3), 内核代码得挂回自己的 0x10
        __asm__ volatile(
            "movw $0x10, %%ax; movw %%ax, %%ds; movw %%ax, %%es; "
            "movw %%ax, %%fs; movw %%ax, %%gs" : : : "ax");
        if (user_killed) {
            serial_puts("[USER] process killed, back to shell\n\r");
            vga_puts("\n\r[USER] process killed (exception) — back to shell\n\r");
        } else {
            serial_puts("[USER] exit code=");
            serial_put_hex(user_exit_code);
            serial_puts("\n\r");
            vga_puts("\n\r[USER] exit code=");
            // 屏幕上也打 (vga_put_hex 在 shell.c 是 static, 用简易版)
            vga_puts("0x");
            for (int i = 28; i >= 0; i -= 4) {
                uint8_t nib = (user_exit_code >> i) & 0xF;
                vga_putc((char)(nib < 10 ? '0' + nib : 'A' + nib - 10));
            }
            vga_puts("\n\r");
        }
        // 【里程碑 11a】不再排空键盘: 老架构要排是因为"触发命令的
        // 回车释放码"会残留在 8042 里; 新架构 IRQ1 处理器吞掉所有
        // 释放码, 运行期间按的键在环形缓冲里排队 (type-ahead),
        // 进程结束后它们正常续打进 shell 输入行。
        shell_reset_input();     // 行缓冲此刻本就是空的, 保险起见清一次
        return;
    }

    user_active = 1;
    serial_puts("[USER] entering ring 3...\n\r");
    enter_user(USER_BASE, USER_BASE + USER_SIZE);
    // 不该到这里: exit 走 longjmp, 异常也走 longjmp
    serial_puts("[USER] ERROR: enter_user returned!\n\r");
    while (1) { __asm__ volatile("hlt"); }
}
