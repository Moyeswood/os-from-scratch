#include "user.h"
#include "proc.h"
#include "paging.h"
#include "memory.h"
#include "vm.h"
#include "serial.h"
#include "vga.h"
#include "timer.h"
#include "keyboard.h"
#include "string.h"
#include "shell.h"
#include "task.h"

// ============================================================
//  Ring 3 用户态 (里程碑 10) + 多进程 (里程碑 11b)
//
//  里程碑 10 的生命周期 (setjmp/longjmp + enter_user 一套) 已被
//  里程碑 11b 取代: 用户进程现在是真正的调度任务 —
//    run <p>     proc_spawn → 独立页目录/用户区/内核栈 → 前台 proc_wait
//    run <p> &   后台: spawn 完就回 shell, 进程和 shell 并发跑
//    exit        → proc_exit: 变 ZOMBIE 切走 (不再 longjmp)
//    异常        → exc.c → proc_fault_kill: 同上, 退出码 -1
//    fork        → proc_fork: 复制地址空间 + 现场, 子进程返回 0
//
//  系统调用是用户程序唯一的内核大门 (int 0x80, 门 DPL=3),
//  调用约定: eax=调用号, ebx/ecx/edx=参数, 返回值写回帧的 eax 槽。
// ============================================================

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
    task_t* me = task_current();
    if ((uint32_t)s < USER_BASE || (uint32_t)s >= USER_BASE + USER_SIZE ||
        !vm_user_range_ok(me, (uint32_t)s, 1)) {
        serial_puts("[SYS] puts: rejected kernel pointer\n\r");
        return -1;
    }
    // 读循环也限在用户区内: 字符串若越过 36MB 边界, 停住返回
    // (否则内核读到未映射页 → 内核态 #PF panic, 用户可借此 DoS)
    const char* region_end = (const char*)(USER_BASE + USER_SIZE);
    while (s < region_end && *s) {
        vga_putc(*s);
        serial_putc(*s);
        s++;
        if (s < region_end && !vm_user_range_ok(me, (uint32_t)s, 1)) {
            serial_puts("\n\r[SYS] puts: rejected unmapped page\n\r");
            return -1;
        }
    }
    return 0;
}

// SYS_GETKEY: 读一个键 (阻塞到有按下事件)。
// 从键盘环形缓冲读 (IRQ1 处理器负责把扫描码推进去)。阻塞 =
// 当前任务 (用户进程自己) 睡到键盘事件: IRQ1 唤醒, 下一个
// IRQ0 把它切上 CPU, 至多 55ms 延迟。
static int sys_getkey(void) {
    return kbd_getchar_blocking(1);   // 1 = 用户语义: ESC → 27
}

// SYS_GETTICKS: 读滴答计数 — 证明用户态期间 IRQ0 定时器照常走
static uint32_t sys_getticks(void) {
    return g_ticks;
}

// ---------- int 0x80 分发 ----------
void syscall_dispatch(user_frame_t* f) {
    switch (f->eax) {
    case SYS_EXIT:
        proc_exit((int)f->ebx);            // 永不返回 (变 ZOMBIE 切走)
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
    case SYS_FORK:
        f->eax = (uint32_t)proc_fork(f);   // 父进程得子 pid, 子进程 (新任务) 得 0
        break;
    case SYS_GETPID:
        f->eax = (uint32_t)task_current()->pid;
        break;
    case SYS_SLEEP:
        task_sleep_ticks(f->ebx);          // 睡 ebx 个滴答 (~55ms 每滴答)
        f->eax = 0;
        break;
    case SYS_BRK:
        f->eax = vm_brk(task_current(), f->ebx);
        break;
    case SYS_MMAP:
        f->eax = vm_mmap(task_current(), f->ebx);
        break;
    default:
        serial_puts("[SYS] unknown syscall #");
        serial_put_hex(f->eax);
        serial_puts("\n\r");
        f->eax = 0xFFFFFFFF;
        break;
    }
}

// ---------- 内核初始化 ----------
void user_init() {
    // 每个进程的虚拟用户区都有独立页目录；物理页由分配器
    // 从当前 E820 managed 区域按需提供，不再预留固定 4MB 块。
    user_progs_init();
    serial_puts("[USER] user region ");
    serial_put_hex(USER_BASE);
    serial_puts("..");
    serial_put_hex(USER_BASE + USER_SIZE);
    serial_puts(" mapped per-process (demand paging + COW)\n\r");
    serial_puts("[USER] per-process VM enabled\n\r");
}

// ---------- run 命令入口 ----------
// 前台: 建进程 → 睡到它退出 → 打印退出码 (旧格式, 测试兼容)
// 后台: 建进程 → 打印 pid → 立刻回 shell (进程和 shell 并发)
static void vga_put_dec(uint32_t n) {
    if (n == 0) { vga_putc('0'); return; }
    char tmp[12];
    int pos = 0;
    while (n) {
        tmp[pos++] = (char)('0' + n % 10);
        n /= 10;
    }
    while (pos) vga_putc(tmp[--pos]);
}

void user_run(const char* name, int background) {
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
        vga_puts("  (try: run hello | count | crash | div0 | echo | badptr | forkdemo | spinner | vmtest | vmguard)\n\r");
        return;
    }
    if (p->size > VM_IMAGE_LIMIT - VM_IMAGE_BASE) {
        serial_puts("[USER] program too big\n\r");
        vga_puts("\n\rrun: program image too large\n\r");
        return;
    }

    int pid = proc_spawn(name);
    if (pid < 0) {
        serial_puts("[USER] spawn failed\n\r");
        vga_puts("\n\rrun: spawn failed (no task slots or out of memory)\n\r");
        return;
    }

    serial_puts("[USER] run ");
    serial_puts(name);
    serial_puts(" (pid=");
    serial_put_hex((uint32_t)pid);
    serial_puts(background ? ") in background\n\r" : ")\n\r");

    if (background) {
        vga_puts("\n\r[USER] started ");
        vga_puts(name);
        vga_puts(" (pid=");
        vga_put_dec((uint32_t)pid);
        vga_puts(") in background\n\r");
        return;
    }

    // 前台: 等它退出 (exit 或被杀都从这里返回)
    int code = proc_wait(pid);
    if (code == -1) {
        serial_puts("[USER] process killed, back to shell\n\r");
        vga_puts("\n\r[USER] process killed (exception) — back to shell\n\r");
    } else {
        serial_puts("[USER] exit code=");
        serial_put_hex((uint32_t)code);
        serial_puts("\n\r");
        vga_puts("\n\r[USER] exit code=0x");
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t nib = ((uint32_t)code >> i) & 0xF;
            vga_putc((char)(nib < 10 ? '0' + nib : 'A' + nib - 10));
        }
        vga_puts("\n\r");
    }
    // 进程期间按的键在环形缓冲里排队 (type-ahead), 正常续打进 shell
    shell_reset_input();
}
