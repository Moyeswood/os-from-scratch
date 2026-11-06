#include "exc.h"
#include "serial.h"
#include "vga.h"
#include "proc.h"
#include "task.h"
#include "vm.h"

// ============================================================
//  异常处理 (里程碑 10: Ring 3 用户态; 里程碑 11b: 杀进程走调度器)
//
//  帧布局 (exc.asm 统一, 从低到高):
//    [0..7]  edi,esi,ebp,esp_orig,ebx,edx,ecx,eax (pusha)
//    [8]     向量号 (桩压入)
//    [9]     错误码 (CPU 压或桩补 0)
//    [10]    EIP   [11] CS   [12] EFLAGS
//    [13]    ESP   [14] SS   (仅用户态异常: 特权级切换时 CPU 压的)
//
//  策略:
//    用户态异常 (CS.RPL=3) → 打印现场 → 杀掉进程 (proc_fault_kill)
//    内核态异常            → 打印现场 → panic (hlt 死循环)
//  里程碑 11b: "杀进程"不再是 longjmp 回 shell, 而是把当前任务
//  标 ZOMBIE 并切走 — 异常发生在进程自己的内核栈上 (TSS.esp0),
//  调度器换帧后这条栈连同异常帧一起被抛弃。
// ============================================================

static const char* exc_names[32] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "(reserved)", "x87 FPU Error", "Alignment Check",
    "Machine Check", "SIMD FP Exception",
    "(reserved)", "(reserved)", "(reserved)", "(reserved)",
    "(reserved)", "(reserved)", "(reserved)", "(reserved)",
    "(reserved)", "(reserved)", "(reserved)", "(reserved)"
};

// 十六进制打印: ★屏幕和串口都要写 — 测试以串口为预言机,
// 异常现场 (EIP/CR2/错误码) 必须在串口可见才能被断言
static void put_hex32(uint32_t n) {
    vga_puts("0x");
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nib = (n >> i) & 0xF;
        char c = (char)(nib < 10 ? '0' + nib : 'A' + nib - 10);
        vga_putc(c);
        serial_putc(c);
    }
}

static void print_both(const char* s) { vga_puts(s); serial_puts(s); }

void exc_dispatch(uint32_t* f) {
    uint32_t vec = f[8], err = f[9];
    uint32_t eip = f[10], cs = f[11], eflags = f[12];
    uint32_t esp = f[13];
    uint32_t cr2 = 0;
    if (vec == 14) __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    // #PF 先交给 VM 层。合法的按需页和 COW 写故障处理后直接
    // 返回异常桩，iret 重试原指令；其余情况才打印现场并杀进程。
    if (vec == 14 && (cs & 3) &&
        vm_handle_page_fault(task_current(), cr2, err))
        return;

    print_both("\n\r[EXC] #");
    // 两位十进制向量号
    vga_putc((char)('0' + vec / 10)); vga_putc((char)('0' + vec % 10));
    serial_putc((char)('0' + vec / 10)); serial_putc((char)('0' + vec % 10));
    print_both(" ");
    print_both(vec < 32 ? exc_names[vec] : "?");
    print_both("\n\r");

    if (vec == 14) {
        // 页错误码解读: bit0=P(存在) bit1=W(写) bit2=U(用户态访问)
        print_both("[EXC]   err=0x");
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t nib = (err >> i) & 0xF;
            char c = (char)(nib < 10 ? '0' + nib : 'A' + nib - 10);
            vga_putc(c); serial_putc(c);
        }
        print_both((err & 4) ? " (user access to supervisor page"
                             : " (kernel-mode fault)");
        if (!(err & 1)) print_both(", page not present");
        if (err & 2)    print_both(", write");
        print_both("\n\r[EXC]   CR2=");
        put_hex32(cr2);
        print_both("\n\r");
    }

    print_both("[EXC]   EIP=");
    put_hex32(eip);
    print_both(" CS=");
    put_hex32(cs);
    print_both(" EFLAGS=");
    put_hex32(eflags);
    print_both("\n\r");

    if (cs & 3) {
        print_both("[EXC]   ESP=");
        put_hex32(esp);
        print_both(" (user)\n\r");
        print_both("[EXC] fault in user mode -> killing process\n\r");
        proc_fault_kill();          // 当前进程变 ZOMBIE 并切走, 不会返回
        print_both("[EXC] ERROR: kill failed\n\r");
    } else {
        print_both("[EXC] fault in KERNEL -> panic, system halted\n\r");
    }

    // 内核态异常: 停在这里 (教学 OS 不尝试恢复内核错误)
    while (1) { __asm__ volatile("hlt"); }
}
