// ============================================================
//  里程碑 11a: 键盘环形缓冲
//
//  老架构: 键盘处理器 (IRQ1) 直接调用 shell_handle_char —
//  整个 shell (连同游戏/编辑器/cc/用户程序) 都跑在 IRQ1 处理
//  链里, 同优先级的 IRQ1 期间不再投递, 所以游戏只能轮询 8042,
//  调度器也只能靠"读 8259A ISR"跳过 IRQ1 在服务中的窗口。
//
//  新架构: IRQ1 处理器只做三件事 — 读 8042 → 推进环形缓冲 →
//  唤醒等键盘的任务。shell 变成可调度的内核线程, 阻塞在
//  kbd_getchar_blocking 上; 游戏/编辑器/cc 从同一个缓冲取键。
//  副产品: 按键在命令执行期间排队 (type-ahead), 释放码天然
//  被吞掉, 老代码里 "kbd_drain 等释放码到达" 的延时舞步全部
//  消失 (drain 现在只是清零两个游标)。
// ============================================================
#include "io.h"
#include "keyboard.h"
#include "proc.h"
#include "task.h"
#include "types.h"

#include "keymap.h"

#define KBD_BUF_SIZE 256

static uint8_t kbd_buf[KBD_BUF_SIZE];
static volatile int kbd_head = 0;   // 写游标 (IRQ1)
static volatile int kbd_tail = 0;   // 读游标 (消费者)

// 翻译状态 (pop 时维护, 见 kbd_pop)
static int shift_pressed = 0;
static int caps_lock_on = 0;
static int ctrl_pressed = 0;
static int irq_ctrl_pressed = 0;

static void kbd_push(uint8_t sc) {
    int next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_tail) return;    // 满: 丢新键 (键盘缓冲的常规策略)
    kbd_buf[kbd_head] = sc;
    kbd_head = next;
}

// IRQ1 入口 (irq.asm 调, EOI 在 irq.asm 里发)
void keyboard_handler() {
    if (!(inb(0x64) & 1)) return;    // 幽灵 IRQ1: 无数据就不读 (读 0x60 会吃到脏字节)
    uint8_t sc = inb(0x60);
    uint8_t key = sc & 0x7F;
    if (key == SC_CTRL) irq_ctrl_pressed = !(sc & 0x80);
    // Ctrl+C 是终端的中断键: 前台作业存在时直接杀整个进程组,
    // 不再把 'c' 塞进输入缓冲。没有前台作业时仍按普通字符处理。
    if (!(sc & 0x80) && irq_ctrl_pressed) {
        if (key == 0x2E && proc_signal_foreground(SIGINT) == 0)
            return;
        if (key == 0x2C && proc_signal_foreground(SIGSTOP) == 0)
            return;
    }
    kbd_push(sc);
    task_wake_kbd();                 // 唤醒等键盘的任务 (下次 IRQ0 把它们切上 CPU)
}

int kbd_pop() {
    if (kbd_tail == kbd_head) return -1;
    uint8_t sc = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;

    // ★翻译状态在 pop 时维护而不是 push 时: 所有消费者 (含读原始
    // 字节的游戏) 都经 kbd_pop, 字节按序经过, shift/caps 状态才不
    // 会因"游戏吞了 shift 按下、shell 只看到释放"而漂移。
    uint8_t key = sc & 0x7F;
    if (key == 0x2A || key == 0x36) shift_pressed = !(sc & 0x80);
    if (key == SC_CTRL) ctrl_pressed = !(sc & 0x80);
    if (key == 0x3A && !(sc & 0x80)) caps_lock_on = !caps_lock_on;
    return sc;
}

// 翻译一个字符事件: 按下码 → 字符 / 上 \x11 / 下 \x12;
// 释放码和未映射键 (F 键/ESC 等) 被消费但不产生事件。
// esc_to_27: ESC 按下 → 27 (用户程序 getkey 的语义); shell 路径
// ESC 无映射 (老行为: shell 忽略 ESC)。
int kbd_getchar(int esc_to_27) {
    while (1) {
        int sc = kbd_pop();
        if (sc < 0) return -1;
        uint8_t key = (uint8_t)(sc & 0x7F);
        if (key == 0x01 && !(sc & 0x80) && esc_to_27) return 27;
        if (sc & 0x80) continue;                 // 释放码
        if (key == 0x48) return '\x11';          // 上方向键
        if (key == 0x50) return '\x12';          // 下方向键
        int is_upper = shift_pressed ^ caps_lock_on;
        char c = is_upper ? keymap_shift[key] : keymap_normal[key];
        if (ctrl_pressed && (c == 'c' || c == 'C')) return 3;
        if (c) return c;
    }
}

// 阻塞版: 空则睡到键盘事件。
// 竞态纪律: 关中断 → 重查缓冲 → 再睡, 否则会漏掉"IRQ1 在
// 查空和入睡之间已经把键推进来了"的窗口 (那个 IRQ1 扫描睡眠者
// 时我们还没睡, 没人会来叫醒我们 → 死等下一个按键)。
int kbd_getchar_blocking(int esc_to_27) {
    for (;;) {
        int c = kbd_getchar(esc_to_27);
        if (c != -1) return c;
        __asm__ volatile("cli");
        c = kbd_getchar(esc_to_27);              // cli 下重查
        if (c != -1) { __asm__ volatile("sti"); return c; }
        task_sleep_switch_locked(TASK_WAIT_KBD, 0);
        __asm__ volatile("sti");
    }
}

// 阻塞等一个"按下"事件 (testmode 的"按任意键", 释放码不算)
int kbd_pop_blocking_press() {
    for (;;) {
        int sc = kbd_pop();
        if (sc >= 0 && sc < 0x80) return sc;
        __asm__ volatile("cli");
        sc = kbd_pop();                          // cli 下重查 (同 kbd_getchar_blocking)
        if (sc >= 0 && sc < 0x80) { __asm__ volatile("sti"); return sc; }
        if (sc >= 0) { __asm__ volatile("sti"); continue; }
        task_sleep_switch_locked(TASK_WAIT_KBD, 0);
        __asm__ volatile("sti");
    }
}

// cc 失控保护: 编译期间缓冲里出现过 ESC 按下?
// 非破坏扫描 (其它排队的键留给 shell 做 type-ahead);
// 命中则清空整个缓冲 (ESC 本身 + 期间乱按的键一起丢掉)。
int kbd_check_esc() {
    int i = kbd_tail;
    while (i != kbd_head) {
        if (kbd_buf[i] == 0x01) { kbd_drain(); return 1; }
        i = (i + 1) % KBD_BUF_SIZE;
    }
    return 0;
}

void kbd_drain() {
    kbd_tail = kbd_head;
    // shift 状态可能因吞掉了配对的释放码而粘住 (例如按住 shift
    // 时 drain), 归零兜底; 下一次真实的 shift 按/放会自愈。
    shift_pressed = 0;
    ctrl_pressed = 0;
}
