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
#include "heap.h"
#include "serial.h"
#include "string.h"

#define PROC_STACK_SIZE  4096

static uint8_t proc_stacks[MAX_TASKS][PROC_STACK_SIZE];

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

static int reap_child(task_t* child) {
    int pid = (int)child->pid;
    int idx = task_slot_of(child);
    vm_destroy(child);
    task_free_slot(idx);
    return pid;
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

int proc_waitpid(int pid, int* status) {
    task_t* parent = task_current();
    if (!parent || !parent->is_user || pid == 0 || pid < -1) return -1;

    while (1) {
        __asm__ volatile("cli");
        task_t* child = find_child(parent, pid, 1);
        if (child) {
            int code = child->exit_code;
            if (status &&
                vm_write_user(parent, (uint32_t)status, &code,
                              sizeof(code)) < 0) {
                __asm__ volatile("sti");
                serial_puts("[PROC] waitpid: status write failed\n\r");
                return -1;
            }
            int result = reap_child(child);
            __asm__ volatile("sti");
            return result;
        }
        if (!find_child(parent, pid, 0)) {
            __asm__ volatile("sti");
            return -1;
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
    child->cwd_inum = parent->cwd_inum;
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

int proc_kill(int pid) {
    task_t* t = task_by_pid(pid);
    if (!t || !t->is_user) return -1;
    serial_puts("[PROC] pid ");
    serial_put_hex(t->pid);
    serial_puts(" (");
    serial_puts(t->name);
    serial_puts(") killed by kill command\n\r");
    detach_children(t->pid);
    // 直接判死: 在跑就跑到下一个滴答 (pick_next 不会再看它),
    // 在睡就永远睡下去; 内存由下一次 spawn 的清扫回收
    t->state = TASK_ZOMBIE;
    t->exit_code = -1;
    t->wait_flags = 0;
    return 0;
}
