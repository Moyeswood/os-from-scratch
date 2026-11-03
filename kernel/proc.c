// ============================================================
//  里程碑 11b: 用户进程管理 (独立地址空间 + fork + 后台进程)
//
//  见 proc.h 头注释。核心技巧:
//
//  1. 草稿窗口 (paging.c): 新进程的物理页在 32MB 之外, 内核
//     恒等映射够不着。写页目录/页表/用户区时, 先用窗口把目标
//     物理页临时映到虚拟 0x7800000, 写完摘掉。★必须 cli 成对
//     使用 (窗口是全局共享的, 被抢占后可能被别的任务改写)。
//
//  2. fork 的现场复制: int 0x80 的帧 (user_frame_t) 恰好就是
//     用户任务的 52 字节上下文帧布局 — 直接抄到子进程内核栈,
//     把 eax 槽改成 0 (子进程返回值), popa+iret 就落在子进程
//     的"int 0x80 之后一条指令", 从 Ring 3 继续跑。
//
//  3. 收尸与释放: 退出时只标 ZOMBIE + 切走 (此时 CR3 已换走,
//     页目录不再被引用); 物理内存由父进程 proc_wait 收尸时释放
//     (父进程跑在自己的 CR3 下, 释放孩子的页安全)。孤儿僵尸
//     (后台进程) 在下次 spawn 时统一清扫。
// ============================================================
#include "proc.h"
#include "task.h"
#include "paging.h"
#include "memory.h"
#include "serial.h"
#include "string.h"

// 用户区连续物理块: 页目录 + 页表8 + 1024 用户页
#define PROC_BLOCK_PAGES 1026
#define PROC_STACK_SIZE  4096

// 教学简化: 用户区 4MB 全映射, 但只初始化程序实际用到的部分 —
//   spawn: 清零低 16KB (镜像 + BSS 预算)
//   fork:  复制低 16KB + 栈区 (顶部 64KB)
// 中段页保持未初始化 (真实 OS 用按需零页/写时复制, 这里留作引子)。
#define PROC_ZERO_BYTES      16384   // 镜像 + BSS 预算
#define PROC_STACK_COPY_BYTES 65536  // 栈预算 (fork 时复制顶部这一段)

static uint8_t proc_stacks[MAX_TASKS][PROC_STACK_SIZE];

// 页目录/页表草稿 (静态缓冲: 内容都是可计算的, 不必先读目标页)
static uint32_t pd_scratch[1024];
static uint32_t pt8_scratch[1024];

// ---------- 查程序表 ----------
static const user_prog_t* prog_lookup(const char* name) {
    for (int i = 0; i < user_prog_count; i++) {
        if (k_strcmp(user_progs[i].name, name) == 0) return &user_progs[i];
    }
    return 0;
}

// 清扫孤儿僵尸 (没人 wait 的后台进程): 释放内存 + 空出槽位。
// 只在 spawn 时调用: 单 CPU 下不会有"正被等待的孩子"被误扫
// (父进程 wait 后孩子才可能变僵尸, 而收尸在 wait 里即时发生)。
static void sweep_zombies(void) {
    for (int i = 5; i < MAX_TASKS; i++) {
        task_t* t = task_at(i);
        if (t && t->state == TASK_ZOMBIE) {
            free_pages(t->user_block, PROC_BLOCK_PAGES);
            task_free_slot(i);
            serial_puts("[PROC] swept orphan zombie (slot ");
            serial_put_hex((uint32_t)i);
            serial_puts(")\n\r");
        }
    }
}

// 页目录/页表草稿内容 (spawn/fork 共用): 内核 PDE0-7 + 窗口 PDE30/31
// + 自己的 PDE8 → 用户区页表
static void fill_scratch(uint32_t block) {
    uint32_t* kpd = (uint32_t*)PAGE_DIR_ADDR;
    for (int i = 0; i < 8; i++) pd_scratch[i] = kpd[i];
    for (int i = 8; i < 30; i++) pd_scratch[i] = 0;
    pd_scratch[30] = kpd[30];
    pd_scratch[31] = kpd[31];
    pd_scratch[8] = (block + 0x1000) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    for (int i = 0; i < 1024; i++) {
        pt8_scratch[i] = (block + 0x2000 + (uint32_t)i * 4096)
                       | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
}

// 在任务槽上搭一个用户进程的初始 Ring 3 帧 (52 字节):
// (低) 8 寄存器 | EIP | CS | EFLAGS | ESP(用户) | SS(用户) (高)
// 首次被调度: popa + iret; iret 见 CS.RPL=3 → 继续弹 ESP/SS,
// 落进用户栈, 从 USER_BASE 开始执行。
static void build_user_frame(task_t* t) {
    uint32_t* stack = (uint32_t*)t->kstack_top;
    *(--stack) = 0x23;                       // SS = 用户数据段
    *(--stack) = USER_BASE + USER_SIZE;      // ESP = 用户栈顶
    *(--stack) = 0x202;                      // EFLAGS: IF=1 (IRQ0 照常滴答)
    *(--stack) = 0x1B;                       // CS = 用户代码段 (RPL=3)
    *(--stack) = USER_BASE;                  // EIP = 用户入口
    for (int i = 0; i < 8; i++) *(--stack) = 0;
    t->esp = (uint32_t)stack;
}

// ---------- 建进程 ----------
static int build_process(const char* name, const user_prog_t* p) {
    sweep_zombies();   // 顺手回收孤儿僵尸的槽位和内存

    int idx = task_alloc_slot();
    if (idx < 0) {
        serial_puts("[PROC] spawn fail: no task slots\n\r");
        return -1;
    }
    uint32_t block = alloc_pages(PROC_BLOCK_PAGES);
    if (!block) {
        task_free_slot(idx);
        serial_puts("[PROC] spawn fail: out of memory (4MB block)\n\r");
        return -1;
    }

    __asm__ volatile("cli");   // ★窗口临界区: 见文件头注释

    // 1. 写页目录 + 页表 8 (窗口映前 2 页)
    fill_scratch(block);
    window_map(block, 2);
    k_memcpy((void*)WINDOW_BASE, pd_scratch, 4096);
    k_memcpy((void*)(WINDOW_BASE + 0x1000), pt8_scratch, 4096);

    // 2. 用户区: 清零低 16KB (镜像+BSS 预算) + 拷镜像
    window_map(block + 0x2000, 1024);
    k_memset((void*)WINDOW_BASE, 0, PROC_ZERO_BYTES);
    k_memcpy((void*)WINDOW_BASE, p->data, p->size);
    window_unmap();

    // 3. 任务槽: 名字/内核栈/CR3/初始帧, 就绪
    task_t* t = task_at(idx);
    k_strcpy(t->name, name);
    t->pid = task_alloc_pid();
    t->kstack_top = proc_stacks[idx] + PROC_STACK_SIZE;
    t->state = TASK_READY;
    t->wait_flags = 0;
    t->cr3 = block;
    t->is_user = 1;
    t->exit_code = 0;
    t->parent_pid = task_current()->pid;   // 父 = shell (或未来的 fork 父)
    t->user_block = block;
    build_user_frame(t);

    __asm__ volatile("sti");

    serial_puts("[PROC] spawn ");
    serial_puts(name);
    serial_puts(" -> pid ");
    serial_put_hex(t->pid);
    serial_puts(" (block=");
    serial_put_hex(block);
    serial_puts(")\n\r");
    return t->pid;
}

// ---------- 对外接口 ----------
int proc_spawn(const char* name) {
    const user_prog_t* p = prog_lookup(name);
    if (!p) return -1;
    return build_process(name, p);
}

// 前台等待: 睡到孩子 ZOMBIE → 收尸 (释放物理块 + 空槽) → 返回退出码
int proc_wait(int pid) {
    task_t* child = task_by_pid(pid);
    if (!child) return -1;

    while (1) {
        __asm__ volatile("cli");
        if (child->state == TASK_ZOMBIE) { __asm__ volatile("sti"); break; }
        task_sleep_switch_locked(TASK_WAIT_PROC, 0);
        __asm__ volatile("sti");
        child = task_by_pid(pid);
        if (!child) return -1;
    }

    int code = child->exit_code;
    uint32_t block = child->user_block;
    int idx = task_slot_of(child);
    free_pages(block, PROC_BLOCK_PAGES);
    task_free_slot(idx);
    return code;
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
    task_zombie_switch_away(-1);     // 永不返回
    while (1) { __asm__ volatile("hlt"); }
}

int proc_fork(user_frame_t* f) {
    int idx = task_alloc_slot();
    if (idx < 0) {
        serial_puts("[PROC] fork fail: no task slots\n\r");
        return -1;
    }
    uint32_t child_block = alloc_pages(PROC_BLOCK_PAGES);
    if (!child_block) {
        task_free_slot(idx);
        serial_puts("[PROC] fork fail: out of memory\n\r");
        return -1;
    }

    __asm__ volatile("cli");   // ★窗口临界区

    // 页目录/页表: 内核页同款 + PDE8 指向子块 (内容可计算, 见 fill_scratch)
    fill_scratch(child_block);
    window_map(child_block, 2);
    k_memcpy((void*)WINDOW_BASE, pd_scratch, 4096);
    k_memcpy((void*)(WINDOW_BASE + 0x1000), pt8_scratch, 4096);

    // 拷贝用户区 (跑在父 CR3 下, 父进程自己的 USER_BASE 直接可读):
    //   低 16KB (程序+BSS) + 栈区 (顶部 64KB, 子进程恢复 fork 现场
    //   后要接着用父进程的调用栈 — 返回地址全在里面)
    window_map(child_block + 0x2000, 1024);
    k_memcpy((void*)WINDOW_BASE, (void*)USER_BASE, PROC_ZERO_BYTES);
    k_memcpy((void*)(WINDOW_BASE + USER_SIZE - PROC_STACK_COPY_BYTES),
             (void*)(USER_BASE + USER_SIZE - PROC_STACK_COPY_BYTES),
             PROC_STACK_COPY_BYTES);
    window_unmap();

    // 子进程任务: 内核栈上的现场 = 父进程的 int 0x80 帧 (52 字节布局
    // 恰好一致), eax 槽改成 0 → 子进程看到 fork() 返回 0
    task_t* parent = task_current();
    task_t* child = task_at(idx);
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

    k_strcpy(child->name, parent->name);
    child->pid = task_alloc_pid();
    child->state = TASK_READY;
    child->wait_flags = 0;
    child->cr3 = child_block;
    child->is_user = 1;
    child->exit_code = 0;
    child->parent_pid = parent->pid;
    child->user_block = child_block;

    __asm__ volatile("sti");

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
    // 直接判死: 在跑就跑到下一个滴答 (pick_next 不会再看它),
    // 在睡就永远睡下去; 内存由下一次 spawn 的清扫回收
    t->state = TASK_ZOMBIE;
    t->exit_code = -1;
    t->wait_flags = 0;
    return 0;
}
