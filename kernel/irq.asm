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
    mov al, 0x20
    out 0x20, al
    iret                        ; 跳到下一个任务被抢占的位置

global irq1_handler
irq1_handler:
    pusha
    call keyboard_handler
    popa

    mov al, 0x20
    out 0x20, al

    iret
