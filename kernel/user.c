#include "user.h"
#include "proc.h"
#include "paging.h"
#include "memory.h"
#include "vm.h"
#include "exec.h"
#include "fs.h"
#include "heap.h"
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

static int user_ptr_ok(uint32_t addr, uint32_t len) {
    uint32_t end = USER_BASE + USER_SIZE;
    return addr >= USER_BASE && addr < end && len <= end - addr;
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

static int copy_user_path(char* out, uint32_t cap, const char* user_path) {
    task_t* me = task_current();
    if (!out || cap == 0 || !user_path) return -1;
    for (uint32_t i = 0; i < cap; ++i) {
        uint32_t addr = (uint32_t)user_path + i;
        if (!vm_user_range_ok(me, addr, 1)) return -1;
        char c = user_path[i];
        out[i] = c;
        if (c == '\0') return 0;
    }
    out[cap - 1] = '\0';
    return -1;
}

static int copy_user_string(char* out, uint32_t cap, const char* source) {
    task_t* me = task_current();
    if (!out || cap == 0 || !source) return -1;
    for (uint32_t i = 0; i < cap; ++i) {
        uint32_t addr = (uint32_t)source + i;
        if (!vm_user_range_ok(me, addr, 1)) return -1;
        char c = source[i];
        out[i] = c;
        if (c == '\0') return 0;
    }
    return -1;
}

static task_fd_t* sys_fd(task_t* t, int fd) {
    if (!t || fd < 0 || fd >= TASK_FD_MAX) return 0;
    if (t->fds[fd].kind == TASK_FD_NONE) return 0;
    return &t->fds[fd];
}

static int sys_fd_alloc(task_t* t) {
    for (int fd = 3; fd < TASK_FD_MAX; fd++)
        if (t->fds[fd].kind == TASK_FD_NONE) return fd;
    return -1;
}

static int sys_open(task_t* me, const char* user_path, uint32_t flags) {
    char path[FS_PATH_MAX];
    if (copy_user_path(path, sizeof(path), user_path) < 0) return -1;

    int readable = (flags & O_RDONLY) != 0;
    int writable = (flags & O_WRONLY) != 0;
    if (!readable && !writable) readable = 1;

    int inum = fs_lookup(path);
    if (inum < 0 && (flags & O_CREAT)) {
        inum = fs_create(path, FS_FILE);
    }
    if (inum < 0 || fs_inode_type((uint32_t)inum) != FS_FILE) return -1;
    if ((flags & O_TRUNC) && writable && fs_truncate((uint32_t)inum) < 0)
        return -1;

    int fd = sys_fd_alloc(me);
    if (fd < 0) return -1;
    task_fd_t* slot = &me->fds[fd];
    slot->kind = TASK_FD_FILE;
    slot->inum = (uint32_t)inum;
    slot->off = 0;
    slot->readable = (uint8_t)readable;
    slot->writable = (uint8_t)writable;
    slot->append = (flags & O_APPEND) != 0;
    serial_puts("[FD] open ");
    serial_puts(path);
    serial_puts(" fd=");
    serial_put_hex((uint32_t)fd);
    serial_puts("\n\r");
    return fd;
}

static int sys_read(task_t* me, int fd, void* user_buf, uint32_t count) {
    task_fd_t* f = sys_fd(me, fd);
    if (!f || !f->readable || !user_buf || !count ||
        !vm_user_range_ok(me, (uint32_t)user_buf, count)) return -1;

    if (f->kind == TASK_FD_STDIN) {
        uint8_t tmp[64];
        uint32_t n = count < sizeof(tmp) ? count : sizeof(tmp);
        uint32_t got = 0;
        while (got < n) {
            int c = sys_getkey();
            if (c < 0) break;
            tmp[got++] = (uint8_t)c;
            if (c == '\n') break;
        }
        if (got && vm_write_user(me, (uint32_t)user_buf, tmp, got) < 0)
            return -1;
        return (int)got;
    }
    if (f->kind != TASK_FD_FILE) return -1;

    uint8_t tmp[128];
    uint32_t left = count;
    uint32_t total = 0;
    while (left) {
        uint32_t chunk = left < sizeof(tmp) ? left : sizeof(tmp);
        int got = fs_read(f->inum, tmp, f->off, chunk);
        if (got < 0) return -1;
        if (!got) break;
        if (vm_write_user(me, (uint32_t)user_buf + total, tmp,
                          (uint32_t)got) < 0) return -1;
        f->off += (uint32_t)got;
        total += (uint32_t)got;
        left -= (uint32_t)got;
    }
    serial_puts("[FD] read fd=");
    serial_put_hex((uint32_t)fd);
    serial_puts(" bytes=");
    serial_put_hex(total);
    serial_puts("\n\r");
    return (int)total;
}

static int sys_write(task_t* me, int fd, const void* user_buf, uint32_t count) {
    task_fd_t* f = sys_fd(me, fd);
    if (!f || !f->writable || !user_buf ||
        !vm_user_range_ok(me, (uint32_t)user_buf, count)) return -1;

    if (f->kind == TASK_FD_STDOUT) {
        const char* p = (const char*)user_buf;
        for (uint32_t i = 0; i < count; i++) {
            vga_putc(p[i]);
            serial_putc(p[i]);
        }
        return (int)count;
    }
    if (f->kind != TASK_FD_FILE) return -1;
    uint32_t off = f->append ? (uint32_t)fs_inode_size((int)f->inum) : f->off;
    int wrote = fs_write_at(f->inum, user_buf, off, count);
    if (wrote < 0) return -1;
    f->off = off + (uint32_t)wrote;
    serial_puts("[FD] write fd=");
    serial_put_hex((uint32_t)fd);
    serial_puts(" bytes=");
    serial_put_hex((uint32_t)wrote);
    serial_puts("\n\r");
    return wrote;
}

static int sys_lseek(task_t* me, int fd, int offset, int whence) {
    task_fd_t* f = sys_fd(me, fd);
    if (!f || f->kind != TASK_FD_FILE) return -1;
    int base;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (int)f->off;
    else if (whence == SEEK_END) base = fs_inode_size((int)f->inum);
    else return -1;
    int target = base + offset;
    if (target < 0) return -1;
    f->off = (uint32_t)target;
    return target;
}

static int sys_close(task_t* me, int fd) {
    task_fd_t* f = sys_fd(me, fd);
    if (!f) return -1;
    k_memset(f, 0, sizeof(*f));
    return 0;
}

static int sys_chdir(task_t* me, const char* user_path) {
    (void)me;
    char path[FS_PATH_MAX];
    if (copy_user_path(path, sizeof(path), user_path) < 0) return -1;
    return fs_chdir(path);
}

static int sys_getcwd(task_t* me, char* user_buf, uint32_t size) {
    if (!user_buf || size == 0 ||
        !vm_user_range_ok(me, (uint32_t)user_buf, size)) return -1;
    char path[FS_PATH_MAX];
    fs_pwd(path);
    uint32_t len = k_strlen(path) + 1;
    if (len > size) return -1;
    return vm_write_user(me, (uint32_t)user_buf, path, len);
}

static int sys_mkdir(task_t* me, const char* user_path) {
    (void)me;
    char path[FS_PATH_MAX];
    if (copy_user_path(path, sizeof(path), user_path) < 0) return -1;
    return fs_create(path, FS_DIR) < 0 ? -1 : 0;
}

static int copy_user_vector(const char* const* source, const char* out[],
                            int max_count, char* storage, uint32_t storage_size,
                            uint32_t* used, int* count) {
    *count = 0;
    if (!source) return 0;

    task_t* me = task_current();
    for (int i = 0; i <= max_count; ++i) {
        uint32_t slot = (uint32_t)source + (uint32_t)i * 4;
        if (!vm_user_range_ok(me, slot, 4)) return -1;
        const char* value = source[i];
        if (!value) return 0;
        if (i == max_count) return -1;
        if (*used >= storage_size) return -1;
        out[i] = storage + *used;
        if (copy_user_string(storage + *used, storage_size - *used,
                             value) < 0)
            return -1;
        *used += k_strlen(out[i]) + 1;
        *count = i + 1;
    }
    return -1;
}

static exec_args_t* copy_exec_args(const char* const* user_argv,
                                   const char* const* user_envp) {
    exec_args_t* args = (exec_args_t*)kmalloc(sizeof(exec_args_t));
    if (!args) return 0;
    k_memset(args, 0, sizeof(*args));

    uint32_t used = 0;
    if (copy_user_vector(user_argv, args->argv, EXEC_MAX_ARGS,
                         args->storage, sizeof(args->storage),
                         &used, &args->argc) < 0 ||
        copy_user_vector(user_envp, args->envp, EXEC_MAX_ENVS,
                         args->storage, sizeof(args->storage),
                         &used, &args->envc) < 0) {
        kfree(args);
        return 0;
    }
    args->argv[args->argc] = 0;
    args->envp[args->envc] = 0;
    return args;
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
    case SYS_EXEC: {
        char path[FS_PATH_MAX];
        if (copy_user_path(path, sizeof(path), (const char*)f->ebx) < 0) {
            serial_puts("[SYS] exec: rejected path pointer\n\r");
            f->eax = 0xFFFFFFFF;
            break;
        }
        exec_args_t* args = copy_exec_args(
            (const char* const*)f->ecx, (const char* const*)f->edx);
        if (!args) {
            f->eax = 0xFFFFFFFFu;
            break;
        }
        f->eax = (uint32_t)exec_replace_current(f, path, args);
        kfree(args);
        break;
    }
    case SYS_WAITPID: {
        int* status = (int*)f->ecx;
        task_t* me = task_current();
        if (status && !vm_user_range_ok(me, (uint32_t)status, sizeof(int))) {
            serial_puts("[SYS] waitpid: rejected status pointer\n\r");
            f->eax = 0xFFFFFFFFu;
            break;
        }
        f->eax = (uint32_t)proc_waitpid((int)f->ebx, status);
        break;
    }
    case SYS_OPEN:
        f->eax = (uint32_t)sys_open(task_current(),
                                    (const char*)f->ebx, f->ecx);
        break;
    case SYS_READ:
        f->eax = (uint32_t)sys_read(task_current(), (int)f->ebx,
                                    (void*)f->ecx, f->edx);
        break;
    case SYS_WRITE:
        f->eax = (uint32_t)sys_write(task_current(), (int)f->ebx,
                                     (const void*)f->ecx, f->edx);
        break;
    case SYS_CLOSE:
        f->eax = (uint32_t)sys_close(task_current(), (int)f->ebx);
        break;
    case SYS_LSEEK:
        f->eax = (uint32_t)sys_lseek(task_current(), (int)f->ebx,
                                     (int)f->ecx, (int)f->edx);
        break;
    case SYS_CHDIR:
        f->eax = (uint32_t)sys_chdir(task_current(), (const char*)f->ebx);
        break;
    case SYS_GETCWD:
        f->eax = (uint32_t)sys_getcwd(task_current(), (char*)f->ebx, f->ecx);
        break;
    case SYS_MKDIR:
        f->eax = (uint32_t)sys_mkdir(task_current(), (const char*)f->ebx);
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
    exec_install_builtins();
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

void user_run(const char* name, int argc, char** argv, int background) {
    const user_prog_t* p = 0;
    for (int i = 0; i < user_prog_count; i++) {
        if (k_strcmp(user_progs[i].name, name) == 0) { p = &user_progs[i]; break; }
    }
    if (p && p->size > VM_IMAGE_LIMIT - VM_IMAGE_BASE) {
        serial_puts("[USER] program too big\n\r");
        vga_puts("\n\rrun: program image too large\n\r");
        return;
    }

    char* envp[4] = { "HOME=/", "PATH=/bin", "NEWOS=1", 0 };
    int pid = p ? proc_spawn_args(name, argc, argv, 3, envp, background)
                : proc_spawn_file_args(name, argc, argv, 3, envp, background);
    if (pid < 0) {
        serial_puts("[USER] unknown program: ");
        serial_puts(name);
        serial_puts("\n\r");
        vga_puts("\n\rrun: unknown executable or spawn failed (try run /bin/hello)\n\r");
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
