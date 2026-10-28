#include "task.h"
#include "vga.h"
#include "io.h"
#include "memory.h"

static uint8_t stack_a[4096];
static uint8_t stack_b[4096];
static uint8_t stack_c[4096];

static task_t kernel_task;
static task_t task_a_struct;
static task_t task_b_struct;
static task_t task_c_struct;
static task_t* current_task;

volatile int multitasking_enabled = 0;

extern void switch_task(task_t* current, task_t* next);

void task_a_main() {
    while(1) {
        if (multitasking_enabled) {
            vga_putc('A');
            // 【优化】：缩短延时，减少打印频率
            for(volatile int i = 0; i < 1000000; i++);
        } else {
            // 多任务未启动时进入 hlt 休眠, 直到下一个中断唤醒
            __asm__ volatile("hlt");
        }
        yield();
    }
}

void task_b_main() {
    while(1) {
        if (multitasking_enabled) {
            vga_putc('B');
            for(volatile int i = 0; i < 1000000; i++);
        } else {
            __asm__ volatile("hlt");
        }
        yield();
    }
}

// 演示任务 C: "恶霸"任务 — 忙等循环, 从不主动 yield
// 协作式调度下它会独占 CPU (其他任务全部饿死),
// 抢占式调度 (IRQ0 定时器) 才能定期打断它
void task_c_main() {
    while(1) {
        if (multitasking_enabled) {
            for(volatile int i = 0; i < 20000000; i++);   // 纯忙等, 故意不让出 CPU
            vga_putc('C');
        } else {
            __asm__ volatile("hlt");
            yield();   // 未启动时正常参与协作轮转
        }
    }
}

// 创建任务: 在栈顶构造初始上下文帧
//   [8 个寄存器(0)] [EIP=入口] [CS=0x08] [EFLAGS=0x202(IF=1)]
// 首次恢复时 popa(全 0) + iret 直接跳进入口函数
static void create_task(task_t* task, void (*entry)(), uint32_t* stack_top) {
    uint32_t* stack = stack_top;
    *(--stack) = 0x202;              // EFLAGS: IF=1
    *(--stack) = 0x08;               // CS = 内核代码段
    *(--stack) = (uint32_t)entry;    // EIP
    for (int i = 0; i < 8; i++) {
        *(--stack) = 0;              // 8 个寄存器
    }
    task->esp = (uint32_t)stack;
}

void task_init() {
    __asm__ volatile("mov %%esp, %0" : "=r"(kernel_task.esp));
    create_task(&task_a_struct, task_a_main, (uint32_t*)(stack_a + 4096));
    create_task(&task_b_struct, task_b_main, (uint32_t*)(stack_b + 4096));
    create_task(&task_c_struct, task_c_main, (uint32_t*)(stack_c + 4096));
    current_task = &kernel_task;
}

void yield() {
    task_t* prev = current_task;

    if (current_task == &kernel_task) {
        current_task = &task_a_struct;
    } else if (current_task == &task_a_struct) {
        current_task = &task_b_struct;
    } else if (current_task == &task_b_struct) {
        current_task = &task_c_struct;
    } else {
        current_task = &kernel_task;
    }

    // 【抢占式安全】切换期间关中断, 防止 IRQ0 在切换中途再次抢占
    __asm__ volatile("cli");
    switch_task(prev, current_task);
    __asm__ volatile("sti");
}

// IRQ0 抢占入口: 保存当前任务帧, 轮转调度, 返回下一个任务的帧指针
// 注意: 参数 esp 就是当前任务的上下文帧指针本身
// (irq.asm 里 mov eax,esp 后 push eax 把帧指针压栈传参)
uint32_t task_timer_tick(uint32_t esp) {
    if (!multitasking_enabled) return esp;

    // 若有其他中断在服务中 (如 IRQ1 键盘 — Shell 命令/俄罗斯方块
    // 都跑在键盘中断处理器内部), 跳过本次抢占, 避免打断它们。
    // 读 8259A ISR: bit0 是 IRQ0 自己, 必须忽略; bit1+ 有值才跳过。
    outb(0x20, 0x0B);                // OCW3: 下一次读返回 ISR 寄存器
    if (inb(0x20) & 0xFE) return esp;

    current_task->esp = esp;
    if (current_task == &kernel_task) {
        current_task = &task_a_struct;
    } else if (current_task == &task_a_struct) {
        current_task = &task_b_struct;
    } else if (current_task == &task_b_struct) {
        current_task = &task_c_struct;
    } else {
        current_task = &kernel_task;
    }
    return current_task->esp;
}

void task_print_status() {
    vga_puts("\n\r  PID  NAME       STATUS\n\r");
    vga_puts("  -------------------------\n\r");

    vga_puts("   0   kernel     ");
    vga_puts(current_task == &kernel_task ? "RUNNING\n\r" : "READY\n\r");

    vga_puts("   1   task_a     ");
    vga_puts(current_task == &task_a_struct ? "RUNNING\n\r" : "READY\n\r");

    vga_puts("   2   task_b     ");
    vga_puts(current_task == &task_b_struct ? "RUNNING\n\r" : "READY\n\r");

    vga_puts("   3   task_c     ");
    vga_puts(current_task == &task_c_struct ? "RUNNING\n\r" : "READY\n\r");

    vga_puts("\n\r");
}
