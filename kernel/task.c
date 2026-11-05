// ============================================================
//  里程碑 11a/11b: PCB 调度器
//
//  里程碑 11a: 取代硬编码的 kernel/A/B/C 四任务轮转 —
//   PCB 数组 + 状态机, shell 变内核线程, 键盘环形缓冲。
//  里程碑 11b: 用户进程也是任务 —
//   - 任务 5+ = 用户进程 (独立页目录 + Ring 3 上下文帧)
//   - 每次切换 = 换帧 + (若切到用户任务) 换 CR3 + 设 TSS.esp0
//     —— 所有切换路径走 switch_to(), 三个入口各管各的帧保存
//   - ZOMBIE 状态: 进程退出后等父进程收尸
//
//  上下文帧: 内核任务 44 字节 (8 寄存器+EIP+CS+EFLAGS),
//  用户任务 52 字节 (再 +ESP+SS — iret 见 CS.RPL=3 会自动弹),
//  恢复方统一 popa+iret, switch.asm/irq.asm 无需区分。
//
//  睡眠竞态纪律: 睡眠者必须 cli → 重查等待条件 → 再睡, 否则
//  会漏掉"数据已到但还没睡"窗口里的 IRQ (见 keyboard.c)。
// ============================================================
#include "task.h"
#include "timer.h"
#include "vga.h"
#include "serial.h"
#include "shell.h"
#include "string.h"
#include "gdt.h"
#include "paging.h"
#include "types.h"

#define SHELL_STACK_SIZE 16384
#define DEMO_STACK_SIZE  4096

static task_t tasks[MAX_TASKS];
static task_t* cur;
static int rr = 0;                // 轮转游标 (公平性)
static int next_pid = 1;          // pid 计数器 (不复用, 防 stale 唤醒)

volatile int multitasking_enabled = 0;

extern void switch_task(task_t* current, task_t* next);

static uint8_t shell_stack[SHELL_STACK_SIZE];
static uint8_t stack_a[DEMO_STACK_SIZE];
static uint8_t stack_b[DEMO_STACK_SIZE];
static uint8_t stack_c[DEMO_STACK_SIZE];

// ---------- 演示任务 A/B/C (老里程碑的抢占演示) ----------
// start 前睡死; start 后 A/B 打印 + 让出, C 是"恶霸"忙等从不让出,
// 只有 IRQ0 抢占能打断它。

void task_a_main() {
    while (1) {
        if (multitasking_enabled) {
            vga_putc('A');
            for (volatile int i = 0; i < 1000000; i++);
        } else {
            task_sleep_forever();
        }
        task_yield();
    }
}

void task_b_main() {
    while (1) {
        if (multitasking_enabled) {
            vga_putc('B');
            for (volatile int i = 0; i < 1000000; i++);
        } else {
            task_sleep_forever();
        }
        task_yield();
    }
}

void task_c_main() {
    while (1) {
        if (multitasking_enabled) {
            for (volatile int i = 0; i < 20000000; i++);   // 纯忙等, 故意不让出
            vga_putc('C');
        } else {
            task_sleep_forever();
        }
    }
}

// 创建内核任务: 在栈顶构造初始上下文帧
//   [8 个寄存器(0)] [EIP=入口] [CS=0x08] [EFLAGS=0x202(IF=1)]
// 首次恢复时 popa(全 0) + iret 直接跳进入口函数
static void create_task(task_t* t, void (*entry)(), uint32_t* stack_top) {
    uint32_t* stack = stack_top;
    *(--stack) = 0x202;              // EFLAGS: IF=1
    *(--stack) = 0x08;               // CS = 内核代码段
    *(--stack) = (uint32_t)entry;    // EIP
    for (int i = 0; i < 8; i++) {
        *(--stack) = 0;              // 8 个寄存器
    }
    t->esp = (uint32_t)stack;
}

// ---------- 调度核心 ----------

// 从轮转游标出发找一个 READY 任务; 没有就绪任务时:
// 当前在跑/就绪(刚让出)/是 idle → 继续当前; 否则 (睡眠/僵尸切换路径) 回 idle
static task_t* pick_next() {
    for (int n = 0; n < MAX_TASKS; n++) {
        rr = (rr + 1) % MAX_TASKS;
        task_t* t = &tasks[rr];
        if (t->state == TASK_READY) return t;
    }
    if (cur->state == TASK_RUNNING || cur->state == TASK_READY || cur->state == TASK_IDLE) {
        return cur;
    }
    return &tasks[0];
}

// 真正执行切换 (调用者已 cli 且已保存 prev 的帧):
//   - 切到用户任务: 换 CR3 + 设 TSS.esp0 = 它的内核栈顶
//     (用户态下的任何中断/系统调用都会落在它自己的栈上)
//   - idle 永远不领状态 (钉死 TASK_IDLE)
//   - ★cur 必须在切换前更新: 醒来后"我是谁"全看它 (11a 最重的坑)
static void switch_to(task_t* prev, task_t* next) {
    if (next->is_user) {
        tss.esp0 = (uint32_t)next->kstack_top;
    }
    if (next->cr3 && next->cr3 != prev->cr3) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(next->cr3));
    }
    if (next != &tasks[0]) next->state = TASK_RUNNING;
    cur = next;
    switch_task(prev, next);
}

void task_init() {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_IDLE;
        tasks[i].esp = 0;
        tasks[i].wait_flags = 0;
        tasks[i].sleep_until = 0;
        tasks[i].pid = 0;
        tasks[i].name[0] = '\0';
        tasks[i].cr3 = PAGE_DIR_ADDR;    // 内核页目录 (0x1000000)
        tasks[i].is_user = 0;
        tasks[i].brk = 0;
        tasks[i].parent_pid = 0;
        tasks[i].exit_code = 0;
        tasks[i].user_block = 0;
    }
    next_pid = 1;

    // 任务 0: idle = kernel_main 的启动栈 (0x90000 向下生长)
    tasks[0].pid = 0;
    k_strcpy(tasks[0].name, "idle");
    __asm__ volatile("mov %%esp, %0" : "=r"(tasks[0].esp));

    // 任务 1: shell 内核线程 (开画面结束前一直睡, 等 task_wake_shell)
    tasks[1].pid = next_pid++;
    k_strcpy(tasks[1].name, "shell");
    tasks[1].state = TASK_SLEEPING;
    tasks[1].kstack_top = shell_stack + SHELL_STACK_SIZE;
    create_task(&tasks[1], shell_main, (uint32_t*)tasks[1].kstack_top);

    // 任务 2~4: 抢占演示 A/B/C (start 前睡死)
    tasks[2].pid = next_pid++; k_strcpy(tasks[2].name, "task_a");
    tasks[2].state = TASK_SLEEPING;
    tasks[2].kstack_top = stack_a + DEMO_STACK_SIZE;
    create_task(&tasks[2], task_a_main, (uint32_t*)tasks[2].kstack_top);
    tasks[3].pid = next_pid++; k_strcpy(tasks[3].name, "task_b");
    tasks[3].state = TASK_SLEEPING;
    tasks[3].kstack_top = stack_b + DEMO_STACK_SIZE;
    create_task(&tasks[3], task_b_main, (uint32_t*)tasks[3].kstack_top);
    tasks[4].pid = next_pid++; k_strcpy(tasks[4].name, "task_c");
    tasks[4].state = TASK_SLEEPING;
    tasks[4].kstack_top = stack_c + DEMO_STACK_SIZE;
    create_task(&tasks[4], task_c_main, (uint32_t*)tasks[4].kstack_top);

    cur = &tasks[0];

    serial_puts("[TASK] scheduler: idle + shell + demo A/B/C (PCB table, 16 slots)\n\r");
}

void sched_start() {
    // 空转循环: 有 READY 任务时 IRQ0 会直接在中断里切过去
    // (task_timer_tick 返回下一个任务的帧); 这里只负责没人可跑时
    // 开门等中断, 省 CPU。
    while (1) {
        __asm__ volatile("sti; hlt");
    }
}

task_t* task_current() {
    return cur;
}

task_t* task_by_pid(int pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid && tasks[i].state != TASK_IDLE) return &tasks[i];
    }
    return 0;
}

task_t* task_at(int idx) {
    if (idx < 0 || idx >= MAX_TASKS) return 0;
    return &tasks[idx];
}

void task_wake_shell() {
    tasks[1].state = TASK_READY;
    tasks[1].wait_flags = 0;
}

// ---------- 里程碑 11b: 任务槽管理 ----------

// 空闲槽 = pid 0 且 IDLE (收尸/重置过的槽位)
int task_alloc_slot() {
    for (int i = 5; i < MAX_TASKS; i++) {
        if (tasks[i].pid == 0 && tasks[i].state == TASK_IDLE) return i;
    }
    return -1;
}

// pid 单调递增不复用: 防"旧 pid 的唤醒/等待串到新主人" (stale wakeup)
int task_alloc_pid() {
    return next_pid++;
}

void task_free_slot(int idx) {
    tasks[idx].state = TASK_IDLE;
    tasks[idx].pid = 0;
    tasks[idx].esp = 0;
    tasks[idx].wait_flags = 0;
    tasks[idx].name[0] = '\0';
    tasks[idx].is_user = 0;
    tasks[idx].cr3 = PAGE_DIR_ADDR;
    tasks[idx].user_block = 0;
    tasks[idx].brk = 0;
    tasks[idx].parent_pid = 0;
    tasks[idx].exit_code = 0;
    tasks[idx].kstack_top = 0;
}

// 子进程退出时调用: 唤醒所有在等 WAIT_PROC 的睡眠者
// (它们醒来后会按 pid 重查, 不是自己要等的人就继续睡)
void task_wake_pid(int pid) {
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && (tasks[i].wait_flags & TASK_WAIT_PROC)) {
            tasks[i].state = TASK_READY;
            tasks[i].wait_flags &= ~TASK_WAIT_PROC;
        }
    }
}

// ---------- 协作切换 ----------

void task_yield() {
    // 抢占式安全: 切换期间关中断, 防止 IRQ0 在切换中途再次抢占
    __asm__ volatile("cli");
    cur->state = TASK_READY;
    task_t* next = pick_next();
    if (next != cur) {
        task_t* prev = cur;
        switch_to(prev, next);
    } else {
        cur->state = TASK_RUNNING;   // 只有自己就绪: 不切换, 继续跑
    }
    __asm__ volatile("sti");
}

void task_sleep_switch_locked(uint32_t wait_flags, uint32_t until) {
    cur->state = TASK_SLEEPING;
    cur->wait_flags = wait_flags;
    cur->sleep_until = until;
    task_t* next = pick_next();   // cur 已睡, 不会被选中; 没人可跑就 idle
    if (next != cur) {
        task_t* prev = cur;
        switch_to(prev, next);
    }
    // 被唤醒后从这里返回 (仍在 cli 状态, 调用者负责 sti)
}

void task_sleep_ticks(uint32_t n) {
    __asm__ volatile("cli");
    task_sleep_switch_locked(TASK_WAIT_TIME, g_ticks + n);
    __asm__ volatile("sti");
}

void task_sleep_forever() {
    __asm__ volatile("cli");
    task_sleep_switch_locked(0, 0);   // 无唤醒条件: 只能显式叫醒
    __asm__ volatile("sti");
}

void task_wake_kbd() {
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && (tasks[i].wait_flags & TASK_WAIT_KBD)) {
            tasks[i].state = TASK_READY;
            tasks[i].wait_flags &= ~TASK_WAIT_KBD;
        }
    }
}

// ---------- 里程碑 11b: 进程退场 ----------

// 当前任务把自己变成 ZOMBIE 并切走, 永不返回。
// (proc_exit 正常退出 / proc_fault_kill 异常被杀共用;
//  ZOMBIE 不会被 pick_next 选中, 内存由父进程收尸时释放 —
//  收尸前 CR3 已换走, 释放页目录才安全)
void task_zombie_switch_away(int exit_code) {
    __asm__ volatile("cli");
    cur->exit_code = exit_code;
    cur->state = TASK_ZOMBIE;
    cur->wait_flags = 0;
    task_wake_pid(cur->pid);
    task_t* next = pick_next();   // ZOMBIE 不会被选中; 没人可跑就 idle
    if (next != cur) {
        task_t* prev = cur;
        switch_to(prev, next);    // 永不返回
    }
    // 理论不可达 (防御)
    while (1) { __asm__ volatile("sti; hlt"); }
}

int task_slot_of(task_t* t) {
    return t - tasks;
}

void task_start_demo() {
    for (int i = 2; i <= 4; i++) {
        tasks[i].state = TASK_READY;
        tasks[i].wait_flags = 0;
    }
}

void task_stop_demo() {
    for (int i = 2; i <= 4; i++) {
        tasks[i].state = TASK_SLEEPING;
        tasks[i].wait_flags = 0;
    }
}

// ---------- IRQ0 抢占 ----------

// 注意: 参数 esp 就是当前任务的上下文帧指针本身
// (irq.asm 里 mov eax,esp 后 push eax 把帧指针压栈传参)
uint32_t task_timer_tick(uint32_t esp) {
    // 唤醒睡到点的任务 (时间睡眠)
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING &&
            (tasks[i].wait_flags & TASK_WAIT_TIME) &&
            g_ticks >= tasks[i].sleep_until) {
            tasks[i].state = TASK_READY;
            tasks[i].wait_flags &= ~TASK_WAIT_TIME;
        }
    }

    cur->esp = esp;
    task_t* next = pick_next();
    if (next != cur) {
        if (cur->state == TASK_RUNNING) cur->state = TASK_READY;
        // 抢占路径的"切换"发生在 irq0_handler 的 popa+iret 里,
        // 这里只做准备工作: 用户任务的 CR3 + TSS.esp0
        if (next->is_user) {
            tss.esp0 = (uint32_t)next->kstack_top;
        }
        if (next->cr3 && next->cr3 != cur->cr3) {
            __asm__ volatile("mov %0, %%cr3" : : "r"(next->cr3));
        }
        if (next != &tasks[0]) next->state = TASK_RUNNING;
        cur = next;                 // ★切换前必须更新 cur (见 11a 记录)
        return next->esp;
    }
    return esp;   // 没有别的就绪任务: 当前继续 (idle 继续睡, 任务继续跑)
}

// ---------- ps ----------

static void put_dec(uint32_t n) {
    if (n == 0) { vga_putc('0'); return; }
    char tmp[12];
    int pos = 0;
    while (n) {
        tmp[pos++] = (char)('0' + n % 10);
        n /= 10;
    }
    while (pos) vga_putc(tmp[--pos]);
}

void task_print_status() {
    vga_puts("\n\r  PID  NAME       STATUS\n\r");
    vga_puts("  -------------------------\n\r");
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == 0 && tasks[i].state == TASK_IDLE) continue;  // 空槽
        vga_puts("  ");
        put_dec(tasks[i].pid);
        vga_puts("   ");
        vga_puts(tasks[i].name);
        for (uint32_t pad = k_strlen(tasks[i].name); pad < 10; pad++) vga_putc(' ');
        switch (tasks[i].state) {
        case TASK_RUNNING:  vga_puts("RUNNING\n\r");  break;
        case TASK_READY:    vga_puts("READY\n\r");    break;
        case TASK_SLEEPING: vga_puts("SLEEPING\n\r"); break;
        case TASK_ZOMBIE:   vga_puts("ZOMBIE\n\r");   break;
        default:            vga_puts("IDLE\n\r");     break;
        }
    }
    vga_puts("\n\r");
}
