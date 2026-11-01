[BITS 32]
section .text

extern keyboard_handler
extern timer_handler

; IRQ0 (定时器): 抢占式调度的入口
; 中断门进来时 CPU 已压 EIP/CS/EFLAGS, pusha 再压 8 个寄存器,
; 形成完整的 44 字节上下文帧 (见 switch.asm 注释)。
; timer_handler 返回下一个任务的帧指针, 这里切过去用 popa+iret 恢复。
global irq0_handler
irq0_handler:
    pusha                       ; 帧 +0..+31
    mov eax, esp                ; eax = 当前任务帧指针
    push eax                    ; 作为参数传给 C
    call timer_handler          ; 返回下一个任务的帧指针
    add esp, 4                  ; 丢弃参数
    mov esp, eax                ; 切换到下一个任务的帧
    popa                        ; 恢复 8 个寄存器
    ; ★EOI 前必须保护 EAX: popa 刚恢复的 EAX 是被中断/被切换上下文的
    ; 现场, mov al,0x20 会把低字节砸成 0x20 — 若上下文正把函数返回值
    ; 放在 EAX 里 (如 selftest 返回 → 调用方存栈之间), 返回值被腐蚀
    ; (乱码 bug 的根因)。push/pop 不改变栈平衡, iret 位置不变。
    push eax
    mov al, 0x20
    out 0x20, al
    pop eax
    iret                        ; 跳到下一个任务被抢占的位置

global irq1_handler
irq1_handler:
    pusha
    call keyboard_handler
    popa

    ; ★同上: EOI 不能砸掉被中断上下文的 EAX (键盘处理完后 shell/
    ; 游戏继续执行时可能正用 EAX 传返回值)
    push eax
    mov al, 0x20
    out 0x20, al
    pop eax

    iret
