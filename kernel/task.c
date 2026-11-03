// ============================================================
//  里程碑 11a: PCB 调度器
//
//  取代硬编码的 kernel/A/B/C 四任务轮转:
//   - PCB 数组 + 状态机 (READY/RUNNING/SLEEPING/IDLE)
//   - 任务 0 = idle (kernel_main 启动栈), 任务 1 = shell 线程,
//     任务 2~4 = 演示任务 A/B/C
//   - 协作切换 (task_yield/睡眠) 与抢占切换 (IRQ0) 共用同一种
//     44 字节上下文帧, 两种路径产出的帧可以互相恢复 (老设计保留)
//   - 老调度器的 "IRQ1 在服务中跳过抢占" hack 删除: 里程碑 11a
//     起 shell/游戏/编辑器都不再跑在 IRQ1 处理链里, 键盘中断
//     只负责把扫描码推进环形缓冲 (keyboard.c)
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
#include "types.h"

#define SHELL_STACK_SIZE 16384
#define DEMO_STACK_SIZE  4096

static task_t tasks[MAX_TASKS];
static task_t* cur;
static int rr = 0;                // 轮转游标 (公平性)

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

// 创建任务: 在栈顶构造初始上下文帧
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
// 当前在跑/就绪(刚让出)/是 idle → 继续当前; 否则 (睡眠切换路径) 回 idle
static task_t* pick_next() {
    for (int n = 0; n < MAX_TASKS; n++) {
        rr = (rr + 1) % MAX_TASKS;
        task_t* t = &tasks[rr];
        if (t->state == TASK_READY) return t;
    }
    if (cur->state != TASK_SLEEPING) return cur;
    return &tasks[0];
}

void task_init() {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_IDLE;
        tasks[i].esp = 0;
        tasks[i].wait_flags = 0;
        tasks[i].sleep_until = 0;
    }

    // 任务 0: idle = kernel_main 的启动栈 (0x90000 向下生长)
    tasks[0].pid = 0;
    tasks[0].name = "idle";
    __asm__ volatile("mov %%esp, %0" : "=r"(tasks[0].esp));

    // 任务 1: shell 内核线程 (开画面结束前一直睡, 等 task_wake_shell)
    tasks[1].pid = 1;
    tasks[1].name = "shell";
    tasks[1].state = TASK_SLEEPING;
    tasks[1].kstack_top = shell_stack + SHELL_STACK_SIZE;
    create_task(&tasks[1], shell_main, (uint32_t*)tasks[1].kstack_top);

    // 任务 2~4: 抢占演示 A/B/C (start 前睡死)
    tasks[2].pid = 2; tasks[2].name = "task_a"; tasks[2].state = TASK_SLEEPING;
    tasks[2].kstack_top = stack_a + DEMO_STACK_SIZE;
    create_task(&tasks[2], task_a_main, (uint32_t*)tasks[2].kstack_top);
    tasks[3].pid = 3; tasks[3].name = "task_b"; tasks[3].state = TASK_SLEEPING;
    tasks[3].kstack_top = stack_b + DEMO_STACK_SIZE;
    create_task(&tasks[3], task_b_main, (uint32_t*)tasks[3].kstack_top);
    tasks[4].pid = 4; tasks[4].name = "task_c"; tasks[4].state = TASK_SLEEPING;
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

void task_wake_shell() {
    tasks[1].state = TASK_READY;
    tasks[1].wait_flags = 0;
}

// ---------- 协作切换 ----------

void task_yield() {
    // 抢占式安全: 切换期间关中断, 防止 IRQ0 在切换中途再次抢占
    __asm__ volatile("cli");
    cur->state = TASK_READY;
    task_t* next = pick_next();
    if (next != cur) {
        if (next != &tasks[0]) next->state = TASK_RUNNING;  // idle 永远不领状态
        task_t* prev = cur;
        cur = next;                 // ★切换前必须更新 cur: 醒来后
        switch_task(prev, next);    //   "我是谁"全看它
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
        if (next != &tasks[0]) next->state = TASK_RUNNING;  // idle 永远不领状态
        task_t* prev = cur;
        cur = next;                 // ★同上
        switch_task(prev, next);
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
        if (next != &tasks[0]) next->state = TASK_RUNNING;  // idle 永远不领状态
        cur = next;                 // ★切换前必须更新 cur (见 task_yield)
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
        if (!tasks[i].name) continue;
        vga_puts("  ");
        put_dec(tasks[i].pid);
        vga_puts("   ");
        vga_puts(tasks[i].name);
        for (uint32_t pad = k_strlen(tasks[i].name); pad < 10; pad++) vga_putc(' ');
        switch (tasks[i].state) {
        case TASK_RUNNING:  vga_puts("RUNNING\n\r");  break;
        case TASK_READY:    vga_puts("READY\n\r");    break;
        case TASK_SLEEPING: vga_puts("SLEEPING\n\r"); break;
        default:            vga_puts("IDLE\n\r");     break;
        }
    }
    vga_puts("\n\r");
}
