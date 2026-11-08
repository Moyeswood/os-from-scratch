// ============================================================
//  里程碑 11b/13: 用户进程管理 (独立虚拟地址空间 + fork + 后台进程)
//
//  见 proc.h/vm.h 头注释。核心技巧:
//
//  1. 每个进程只有独立页目录和实际触及的页表。镜像页在 spawn
//     时装入；栈、堆、mmap 页由 #PF 按需补零。
//
//  2. fork 只复制页表，物理页先共享并标 COW。用户写共享页时
//     #PF 再分配新页并复制 4KB。
//
//  3. fork 的现场复制: int 0x80 的帧 (user_frame_t) 恰好就是
//     用户任务的 52 字节上下文帧布局 — 直接抄到子进程内核栈,
//     把 eax 槽改成 0 (子进程返回值), popa+iret 就落在子进程
//     的"int 0x80 之后一条指令", 从 Ring 3 继续跑。
//
//  4. 收尸与释放: 退出时只标 ZOMBIE + 切走 (此时 CR3 已换走,
//     页目录不再被引用); 物理内存由父进程 proc_wait 收尸时释放
//     (父进程跑在自己的 CR3 下, 释放孩子的页安全)。孤儿僵尸
//     (后台进程) 在下次 spawn 时统一清扫。
// ============================================================
#include "proc.h"
#include "task.h"
#include "paging.h"
#include "memory.h"
#include "vm.h"
#include "exec.h"
#include "fs.h"
#include "heap.h"
#include "serial.h"
#include "string.h"

#define PROC_STACK_SIZE  4096

static uint8_t proc_stacks[MAX_TASKS][PROC_STACK_SIZE];
static int console_fg_pgid = 0;

#define SIGNAL_BIT(sig) (1u << ((uint32_t)(sig) - 1))
#define SIGFRAME_MAGIC 0x53474652u   // "SGFR"

typedef struct {
    user_frame_t saved;
    uint32_t saved_mask;
    uint32_t signal;
    uint32_t magic;
} user_signal_frame_t;

// ---------- 查程序表 ----------
static const user_prog_t* prog_lookup(const char* name) {
    for (int i = 0; i < user_prog_count; i++) {
        if (k_strcmp(user_progs[i].name, name) == 0) return &user_progs[i];
    }
    return 0;
}

// 清扫孤儿僵尸 (后台进程或父进程已消失): 释放内存 + 空出槽位。
// 仍有存活父进程的 ZOMBIE 必须保留到 waitpid 收尸。
static void sweep_zombies(void) {
    for (int i = 5; i < MAX_TASKS; i++) {
        task_t* t = task_at(i);
        task_t* parent = (t && t->parent_pid) ?
                         task_by_pid(t->parent_pid) : 0;
        int orphan = !parent || parent->state == TASK_ZOMBIE;
        if (t && t->state == TASK_ZOMBIE && orphan) {
            vm_destroy(t);
            task_free_slot(i);
            serial_puts("[PROC] swept orphan zombie (slot ");
            serial_put_hex((uint32_t)i);
            serial_puts(")\n\r");
        }
    }
}

// 在任务槽上搭一个用户进程的初始 Ring 3 帧 (52 字节):
// (低) 8 寄存器 | EIP | CS | EFLAGS | ESP(用户) | SS(用户) (高)
// 首次被调度: popa + iret; iret 见 CS.RPL=3 → 继续弹 ESP/SS,
// 落进用户栈, 从 USER_BASE 开始执行。
static void build_user_frame(task_t* t, uint32_t entry, uint32_t user_esp) {
    uint32_t* stack = (uint32_t*)t->kstack_top;
    *(--stack) = 0x23;                       // SS = 用户数据段
    *(--stack) = user_esp;
    *(--stack) = 0x202;                      // EFLAGS: IF=1 (IRQ0 照常滴答)
    *(--stack) = 0x1B;                       // CS = 用户代码段 (RPL=3)
    *(--stack) = entry;
    for (int i = 0; i < 8; i++) *(--stack) = 0;
    t->esp = (uint32_t)stack;
}

// ---------- 建进程 ----------
static int build_process(const char* name, const uint8_t* image,
                         uint32_t image_size, int argc, char* const* argv,
                         int envc, char* const* envp, int detached) {
    sweep_zombies();   // 顺手回收孤儿僵尸的槽位和内存

    if (!image || image_size == 0) {
        serial_puts("[PROC] spawn fail: empty program image\n\r");
        return -1;
    }
    if (image_size > VM_IMAGE_LIMIT - VM_IMAGE_BASE) {
        serial_puts("[PROC] spawn fail: program image > 256KB\n\r");
        return -1;
    }

    int idx = task_alloc_slot();
    if (idx < 0) {
        serial_puts("[PROC] spawn fail: no task slots\n\r");
        return -1;
    }

    // 任务槽: 名字/内核栈/初始帧。地址空间只分配镜像实际页；
    // 栈、堆和 mmap 都由首次访问触发 #PF 补页。
    task_t* t = task_at(idx);
    k_strcpy(t->name, name);
    t->pid = task_alloc_pid();
    t->kstack_top = proc_stacks[idx] + PROC_STACK_SIZE;
    t->state = TASK_IDLE;   // 地址空间完整前绝不能被调度
    t->wait_flags = 0;
    t->is_user = 1;
    t->exit_code = 0;
    t->parent_pid = detached ? 0 : (int)task_current()->pid;
    t->pgid = (int)t->pid;   // 新程序默认独立进程组, 父进程可再合并流水线
    t->cwd_inum = task_current()->cwd_inum;
    task_fd_reset(t);

    uint32_t entry = 0;
    uint32_t initial_esp = 0;
    if (vm_init_program(t, image, image_size, &entry) < 0) {
        task_free_slot(idx);
        serial_puts("[PROC] spawn fail: virtual address space\n\r");
        return -1;
    }
    if (vm_build_initial_stack(t, argc, (const char* const*)argv,
                               envc, (const char* const*)envp,
                               &initial_esp) < 0) {
        vm_destroy(t);
        task_free_slot(idx);
        serial_puts("[PROC] spawn fail: initial stack\n\r");
        return -1;
    }
    build_user_frame(t, entry, initial_esp);
    t->state = TASK_READY;

    serial_puts("[PROC] spawn ");
    serial_puts(name);
    serial_puts(" -> pid ");
    serial_put_hex(t->pid);
    serial_puts(" (pd=");
    serial_put_hex(t->cr3);
    serial_puts(")\n\r");
    return t->pid;
}

// ---------- 对外接口 ----------
int proc_spawn(const char* name) {
    char* argv[1] = { (char*)name };
    return proc_spawn_args(name, 1, argv, 0, 0, 0);
}

int proc_spawn_args(const char* name, int argc, char* const* argv,
                    int envc, char* const* envp, int detached) {
    const user_prog_t* p = prog_lookup(name);
    if (!p) return -1;
    return build_process(name, p->data, p->size, argc, argv, envc, envp,
                         detached);
}

int proc_spawn_file(const char* path) {
    char* argv[1] = { (char*)path };
    return proc_spawn_file_args(path, 1, argv, 0, 0, 0);
}

int proc_spawn_file_args(const char* path, int argc, char* const* argv,
                         int envc, char* const* envp, int detached) {
    uint8_t* image = 0;
    uint32_t size = 0;
    if (exec_load_file(path, &image, &size) < 0) return -1;

    char name[16];
    exec_basename(name, sizeof(name), path);
    int pid = build_process(name, image, size, argc, argv, envc, envp,
                            detached);
    kfree(image);
    return pid;
}

static task_t* find_child(task_t* parent, int requested_pid, int zombie_only) {
    if (!parent) return 0;
    for (int i = 5; i < MAX_TASKS; ++i) {
        task_t* t = task_at(i);
        if (!t || !t->is_user ||
            t->parent_pid != (int)parent->pid) continue;
        if (requested_pid != -1 && (int)t->pid != requested_pid) continue;
        if (zombie_only && t->state != TASK_ZOMBIE) continue;
        return t;
    }
    return 0;
}

static task_t* find_stopped_child(task_t* parent, int requested_pid) {
    if (!parent) return 0;
    for (int i = 5; i < MAX_TASKS; ++i) {
        task_t* t = task_at(i);
        if (!t || !t->is_user ||
            t->parent_pid != (int)parent->pid ||
            t->state != TASK_STOPPED) continue;
        if (requested_pid != -1 && (int)t->pid != requested_pid) continue;
        return t;
    }
    return 0;
}

static int reap_child(task_t* child) {
    int pid = (int)child->pid;
    int idx = task_slot_of(child);
    vm_destroy(child);
    task_free_slot(idx);
    return pid;
}

static int write_wait_status(task_t* parent, int* status, int value) {
    if (!status) return 0;
    if (vm_write_user(parent, (uint32_t)status, &value, sizeof(value)) < 0) {
        serial_puts("[PROC] waitpid: status write failed\n\r");
        return -1;
    }
    return 0;
}

static void detach_children(int parent_pid) {
    for (int i = 5; i < MAX_TASKS; ++i) {
        task_t* t = task_at(i);
        if (t && t->is_user && t->parent_pid == parent_pid)
            t->parent_pid = 0;
    }
}

// 前台等待: 睡到孩子 ZOMBIE → 收尸 (销毁地址空间 + 空槽) → 返回退出码
int proc_wait(int pid) {
    task_t* parent = task_current();
    task_t* child = find_child(parent, pid, 0);
    if (!child) return -1;

    while (1) {
        __asm__ volatile("cli");
        child = find_child(parent, pid, 1);
        if (child) break;
        if (!find_child(parent, pid, 0)) {
            __asm__ volatile("sti");
            return -1;
        }
        task_sleep_switch_locked(TASK_WAIT_PROC, 0);
        __asm__ volatile("sti");
    }

    int code = child->exit_code;
    reap_child(child);
    return code;
}

int proc_waitpid(int pid, int* status, int flags) {
    task_t* parent = task_current();
    if (!parent || !parent->is_user || pid == 0 || pid < -1) return -1;

    while (1) {
        __asm__ volatile("cli");
        task_t* child = find_child(parent, pid, 1);
        if (child) {
            int code = child->exit_code;
            if (write_wait_status(parent, status, code) < 0) {
                __asm__ volatile("sti");
                return -1;
            }
            int result = reap_child(child);
            __asm__ volatile("sti");
            return result;
        }
        if (flags & 2) {   // WUNTRACED
            child = find_stopped_child(parent, pid);
            if (child) {
                int code = (SIGSTOP << 8) | 0x7F;
                if (write_wait_status(parent, status, code) < 0) {
                    __asm__ volatile("sti");
                    return -1;
                }
                int result = (int)child->pid;
                __asm__ volatile("sti");
                return result;
            }
        }
        if (!find_child(parent, pid, 0)) {
            __asm__ volatile("sti");
            return -1;
        }
        if (flags & 1) {   // WNOHANG: 没有已退出的子进程就立即返回
            __asm__ volatile("sti");
            return 0;
        }
        task_sleep_switch_locked(TASK_WAIT_PROC, 0);
        __asm__ volatile("sti");
    }
}

void proc_exit(int code) {
    task_t* me = task_current();
    serial_puts("[PROC] pid ");
    serial_put_hex(me->pid);
    serial_puts(" (");
    serial_puts(me->name);
    serial_puts(") exited code=");
    serial_put_hex((uint32_t)code);
    serial_puts("\n\r");
    task_fd_close_all(me);       // 先关写端, 让管道读者看到 EOF
    fs_flush();                  // 正常退出是文件 close 的持久化边界
    detach_children(me->pid);
    task_zombie_switch_away(code);   // 永不返回
    while (1) { __asm__ volatile("hlt"); }
}

void proc_fault_kill(void) {
    task_t* me = task_current();
    if (!me->is_user) {   // 防御: 内核任务不该走这条路
        serial_puts("[PROC] ERROR: fault kill on kernel task\n\r");
        while (1) { __asm__ volatile("hlt"); }
    }
    serial_puts("[PROC] pid ");
    serial_put_hex(me->pid);
    serial_puts(" (");
    serial_puts(me->name);
    serial_puts(") killed by exception\n\r");
    task_fd_close_all(me);
    detach_children(me->pid);
    task_zombie_switch_away(-1);     // 永不返回
    while (1) { __asm__ volatile("hlt"); }
}

int proc_fork(user_frame_t* f) {
    task_t* parent = task_current();
    int idx = task_alloc_slot();
    if (idx < 0) {
        serial_puts("[PROC] fork fail: no task slots\n\r");
        return -1;
    }

    // 子进程任务: 内核栈上的现场 = 父进程的 int 0x80 帧 (52 字节布局
    // 恰好一致), eax 槽改成 0 → 子进程看到 fork() 返回 0
    task_t* child = task_at(idx);
    k_strcpy(child->name, parent->name);
    child->pid = task_alloc_pid();
    child->state = TASK_IDLE;   // 帧/页表完整前不可调度
    child->wait_flags = 0;
    child->is_user = 1;
    child->exit_code = 0;
    child->parent_pid = parent->pid;
    child->pgid = parent->pgid;
    child->cwd_inum = parent->cwd_inum;
    child->signal_pending = parent->signal_pending;
    child->signal_mask = parent->signal_mask;
    child->signal_restorer = parent->signal_restorer;
    k_memcpy(child->signal_handlers, parent->signal_handlers,
             sizeof(child->signal_handlers));
    task_fd_copy(child, parent);

    if (vm_clone(parent, child) < 0) {
        task_free_slot(idx);
        serial_puts("[PROC] fork fail: virtual address space\n\r");
        return -1;
    }

    child->kstack_top = proc_stacks[idx] + PROC_STACK_SIZE;   // ★先设栈再建帧
    uint32_t* stack = (uint32_t*)child->kstack_top;
    *(--stack) = f->ss_user;
    *(--stack) = f->esp_user;
    *(--stack) = f->eflags;
    *(--stack) = f->cs;
    *(--stack) = f->eip;
    *(--stack) = 0;                 // eax = 0 (子进程返回值)
    *(--stack) = f->ecx;
    *(--stack) = f->edx;
    *(--stack) = f->ebx;
    *(--stack) = f->esp_orig;
    *(--stack) = f->ebp;
    *(--stack) = f->esi;
    *(--stack) = f->edi;
    child->esp = (uint32_t)stack;
    child->state = TASK_READY;

    serial_puts("[PROC] fork: pid ");
    serial_put_hex(parent->pid);
    serial_puts(" -> child pid ");
    serial_put_hex(child->pid);
    serial_puts("\n\r");
    return child->pid;   // 父进程看到子 pid
}

static int kill_task(task_t* t, const char* reason) {
    if (!t || !t->is_user || t->state == TASK_ZOMBIE) return -1;
    serial_puts("[PROC] pid ");
    serial_put_hex(t->pid);
    serial_puts(" (");
    serial_puts(t->name);
    serial_puts(") killed by ");
    serial_puts(reason);
    serial_puts("\n\r");
    task_fd_close_all(t);
    detach_children(t->pid);
    // 直接判死: 在跑就跑到下一个滴答 (pick_next 不会再看它),
    // 在睡就永远睡下去; 内存由下一次 spawn 的清扫回收
    t->state = TASK_ZOMBIE;
    t->exit_code = -1;
    t->wait_flags = 0;
    task_wake_pid(t->pid);
    return 0;
}

static int stop_task(task_t* t) {
    if (!t || !t->is_user || t->state == TASK_ZOMBIE ||
        t->state == TASK_STOPPED) return -1;
    serial_puts("[PROC] pid ");
    serial_put_hex(t->pid);
    serial_puts(" (");
    serial_puts(t->name);
    serial_puts(") stopped by SIGSTOP\n\r");
    t->state = TASK_STOPPED;
    task_wake_pid(t->pid);
    return 0;
}

static int continue_task(task_t* t) {
    if (!t || !t->is_user || t->state != TASK_STOPPED) return -1;
    serial_puts("[PROC] pid ");
    serial_put_hex(t->pid);
    serial_puts(" (");
    serial_puts(t->name);
    serial_puts(") resumed by SIGCONT\n\r");
    t->state = TASK_READY;
    t->wait_flags = 0;
    t->sleep_until = 0;
    task_wake_pid(t->pid);
    return 0;
}

int proc_kill(int pid) {
    return kill_task(task_by_pid(pid), "kill command");
}

int proc_setpgid(int pid, int pgid) {
    task_t* me = task_current();
    if (!me || !me->is_user) return -1;
    task_t* target = pid ? task_by_pid(pid) : me;
    if (!target || !target->is_user || target->state == TASK_ZOMBIE)
        return -1;
    if (pid && target->parent_pid != (int)me->pid &&
        target != me) return -1;
    if (!pgid) pgid = (int)target->pid;
    if (pgid <= 0) return -1;
    target->pgid = pgid;
    return 0;
}

int proc_set_foreground(int pgid) {
    if (pgid < 0) return -1;
    console_fg_pgid = pgid;
    return 0;
}

static int signal_supported(int sig) {
    return sig == SIGINT || sig == SIGKILL || sig == SIGTERM ||
           sig == SIGSTOP || sig == SIGCONT;
}

int proc_sigaction(int sig, uint32_t handler, uint32_t restorer) {
    task_t* t = task_current();
    if (!t || !t->is_user || !signal_supported(sig) ||
        sig == SIGKILL || sig == SIGSTOP)
        return -1;
    if (handler != SIG_DFL && handler != SIG_IGN &&
        !vm_user_range_ok(t, handler, 1))
        return -1;
    if (handler != SIG_DFL && handler != SIG_IGN &&
        !vm_user_range_ok(t, restorer, 1))
        return -1;

    uint32_t old = t->signal_handlers[sig - 1];
    t->signal_handlers[sig - 1] = handler;
    if (handler != SIG_DFL && handler != SIG_IGN)
        t->signal_restorer = restorer;
    serial_puts("[SIG] pid ");
    serial_put_hex(t->pid);
    serial_puts(" sig=");
    serial_put_hex((uint32_t)sig);
    serial_puts(" action=");
    serial_put_hex(handler);
    serial_puts("\n\r");
    return (int)old;
}

int proc_sigprocmask(int how, uint32_t set, uint32_t* oldset) {
    task_t* t = task_current();
    if (!t || !t->is_user) return -1;
    if (oldset && !vm_user_range_ok(t, (uint32_t)oldset, sizeof(uint32_t)))
        return -1;
    if (set && !vm_user_range_ok(t, set, sizeof(uint32_t))) return -1;

    uint32_t old = t->signal_mask;
    uint32_t mask = set ? *(const uint32_t*)set : 0;
    uint32_t immutable = SIGNAL_BIT(SIGKILL) | SIGNAL_BIT(SIGSTOP);
    mask &= ~immutable;
    if (how == SIG_BLOCK) t->signal_mask |= mask;
    else if (how == SIG_UNBLOCK) t->signal_mask &= ~mask;
    else if (how == SIG_SETMASK) t->signal_mask = mask;
    else return -1;
    t->signal_mask &= ~immutable;
    if (oldset && vm_write_user(t, (uint32_t)oldset, &old, sizeof(old)) < 0)
        return -1;
    return 0;
}

int proc_sigreturn(user_frame_t* f) {
    task_t* t = task_current();
    if (!t || !t->is_user || !f || !(f->cs & 3)) return -1;
    user_signal_frame_t* frame =
        (user_signal_frame_t*)(f->esp_user + 8);
    if (!vm_user_range_ok(t, (uint32_t)frame, sizeof(*frame)) ||
        frame->magic != SIGFRAME_MAGIC || !(frame->saved.cs & 3))
        return -1;
    user_frame_t saved = frame->saved;
    t->signal_mask = frame->saved_mask;
    frame->magic = 0;
    *f = saved;
    return 0;
}

static int signal_deliver_one(task_t* t, user_frame_t* f) {
    if (!t || !t->is_user || !f || !(f->cs & 3)) return 0;
    uint32_t ready = t->signal_pending & ~t->signal_mask;
    if (!ready) return 0;

    int sig = 1;
    while (sig <= TASK_SIGNAL_MAX && !(ready & SIGNAL_BIT(sig))) sig++;
    if (sig > TASK_SIGNAL_MAX) return 0;
    uint32_t handler = t->signal_handlers[sig - 1];
    if (handler == SIG_IGN) {
        t->signal_pending &= ~SIGNAL_BIT(sig);
        return 1;
    }
    if (handler == SIG_DFL) {
        t->signal_pending &= ~SIGNAL_BIT(sig);
        if (sig == SIGSTOP) {
            stop_task(t);
        } else if (sig == SIGCONT) {
            continue_task(t);
        } else {
            kill_task(t, sig == SIGINT ? "SIGINT" : "SIGTERM");
        }
        return 1;
    }
    if (!t->signal_restorer ||
        !vm_user_range_ok(t, handler, 1) ||
        !vm_user_range_ok(t, t->signal_restorer, 1))
        return 0;

    uint32_t old_esp = f->esp_user;
    if (old_esp < VM_STACK_BOTTOM + 128)
        return 0;
    uint32_t base = (old_esp - sizeof(user_signal_frame_t) - 16) & ~15u;
    uint32_t ret_addr = base - 4;
    user_signal_frame_t frame;
    frame.saved = *f;
    frame.saved_mask = t->signal_mask;
    frame.signal = (uint32_t)sig;
    frame.magic = SIGFRAME_MAGIC;

    if (vm_write_user(t, ret_addr, &t->signal_restorer, sizeof(uint32_t)) < 0 ||
        vm_write_user(t, base, &sig, sizeof(uint32_t)) < 0 ||
        vm_write_user(t, base + 8, &frame, sizeof(frame)) < 0)
        return 0;

    t->signal_pending &= ~SIGNAL_BIT(sig);
    t->signal_mask |= SIGNAL_BIT(sig);
    f->esp_user = ret_addr;
    f->eip = handler;
    f->eax = (uint32_t)sig;
    serial_puts("[SIG] deliver pid ");
    serial_put_hex(t->pid);
    serial_puts(" sig=");
    serial_put_hex((uint32_t)sig);
    serial_puts(" eip=");
    serial_put_hex(handler);
    serial_puts("\n\r");
    return 1;
}

void proc_deliver_signal(user_frame_t* f) {
    task_t* t = task_current();
    while (signal_deliver_one(t, f)) {
        if (t->state == TASK_STOPPED || t->state == TASK_ZOMBIE) break;
    }
}

void proc_deliver_current_saved(void) {
    task_t* t = task_current();
    if (!t || !t->is_user || !t->esp) return;
    proc_deliver_signal((user_frame_t*)t->esp);
}

void proc_reset_signals(void) {
    task_t* t = task_current();
    if (!t) return;
    t->signal_pending = 0;
    t->signal_mask = 0;
    t->signal_restorer = 0;
    k_memset(t->signal_handlers, 0, sizeof(t->signal_handlers));
}

int proc_signal_pending_current(void) {
    task_t* t = task_current();
    if (!t || !t->is_user) return 0;
    uint32_t ready = t->signal_pending & ~t->signal_mask;
    if (!ready) return 0;
    for (int sig = 1; sig <= TASK_SIGNAL_MAX; sig++) {
        if (!(ready & SIGNAL_BIT(sig))) continue;
        if (t->signal_handlers[sig - 1] > SIG_IGN) return 1;
    }
    return 0;
}

static int send_signal_to_task(task_t* t, int sig) {
    if (!t || !t->is_user || t->state == TASK_ZOMBIE) return -1;
    if (sig == SIGKILL) return kill_task(t, "SIGKILL");
    if (sig == SIGSTOP) {
        if (t->state == TASK_STOPPED) return 0;
        return stop_task(t);
    }
    if (sig == SIGCONT) {
        if (t->state == TASK_STOPPED) continue_task(t);
        if (t->signal_handlers[sig - 1] == SIG_DFL ||
            t->signal_handlers[sig - 1] == SIG_IGN)
            return 0;
    }

    uint32_t handler = t->signal_handlers[sig - 1];
    if (handler == SIG_IGN) return 0;
    if (handler == SIG_DFL) {
        return kill_task(t, sig == SIGINT ? "SIGINT" : "SIGTERM");
    }

    t->signal_pending |= SIGNAL_BIT(sig);
    if (t->state == TASK_SLEEPING) {
        t->state = TASK_READY;
        t->wait_flags = 0;
    }
    serial_puts("[SIG] queued pid ");
    serial_put_hex(t->pid);
    serial_puts(" sig=");
    serial_put_hex((uint32_t)sig);
    serial_puts("\n\r");
    return 0;
}

int proc_signal(int pid, int sig) {
    if (!pid || !signal_supported(sig))
        return -1;
    int affected = 0;
    int direction = pid < 0 ? -pid : pid;
    if (pid < 0) {
        for (int i = 5; i < MAX_TASKS; ++i) {
            task_t* t = task_at(i);
            if (!t || !t->is_user || t->pgid != direction ||
                t->state == TASK_ZOMBIE) continue;
            if (send_signal_to_task(t, sig) == 0) ++affected;
        }
    } else {
        task_t* t = task_by_pid(pid);
        if (t && t->is_user && t->state != TASK_ZOMBIE) {
            if (send_signal_to_task(t, sig) == 0) ++affected;
        }
    }
    return affected ? 0 : -1;
}

int proc_signal_foreground(int sig) {
    if (console_fg_pgid <= 0) return -1;
    serial_puts("[PROC] console signal pgid=");
    serial_put_hex((uint32_t)console_fg_pgid);
    serial_puts(" sig=");
    serial_put_hex((uint32_t)sig);
    serial_puts("\n\r");
    return proc_signal(-console_fg_pgid, sig);
}
