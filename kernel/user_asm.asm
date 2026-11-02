; ============================================================
;  用户态相关汇编 (里程碑 10: Ring 3 用户态)
;
;  gdt_reload_segments: 加载新 GDT 后刷新段寄存器 (gdt_init 调)
;  enter_user:          iret 进 Ring 3 (user.c 调, 永不"正常返回")
;  syscall_handler:     int 0x80 入口 (用户程序唯一的内核大门)
; ============================================================

[BITS 32]
section .text

extern tss              ; gdt.c 的全局 TSS (esp0 在偏移 4)
extern syscall_dispatch ; user.c: void syscall_dispatch(uint32_t* frame)

; ---- 加载新 GDT 后: ljmp 刷 CS, 数据段全部重载 0x10 ----
global gdt_reload_segments
gdt_reload_segments:
    jmp 0x08:.flush     ; 远跳 → CS = 0x08 (新表的内核代码段)
.flush:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

; ============================================================
;  enter_user(uint32_t entry, uint32_t esp_user)
;
;  iret 到 Ring 3 需要栈上摆好一个完整的返回帧:
;  (低地址) SS, ESP, EFLAGS, CS, EIP (高地址)
;  CPU 执行 iret 时依次弹出: EIP←(栈), CS←, EFLAGS←,
;  发现 CS 的 RPL=3 > CPL=0 → 特权级提升方向的反向切换,
;  继续弹 ESP/SS → 回到用户栈。就这么"降落"到 Ring 3。
;
;  ★TSS.esp0 = 此刻的内核 esp: 用户态期间来的任何中断/
;  系统调用, CPU 都会先切到这个栈再压返回帧。它正好是
;  "user_run → enter_user"这条调用链的栈底, 所以异常处理
;  能 longjmp 回 shell 的命令执行流程 (栈是连续的)。
; ============================================================
global enter_user
enter_user:
    mov eax, [esp + 4]      ; entry (用户代码入口 = USER_BASE)
    mov ecx, [esp + 8]      ; esp_user (用户栈顶)
    cli                     ; 摆帧期间不许中断

    push 0x23               ; SS = 用户数据段
    push ecx                ; ESP = 用户栈顶
    pushf
    pop edx
    or edx, 0x200           ; EFLAGS.IF = 1: 用户态下 IRQ0 继续滴答
                            ; (get_ticks 演示依赖它; 不置位用户态永远不开中断)
    push edx                ; EFLAGS
    push 0x1B               ; CS = 用户代码段 (RPL=3)
    push eax                ; EIP = 用户入口

    mov [tss + 4], esp      ; TSS.esp0 = 内核恢复栈 (见头注释)

    mov ax, 0x23            ; 数据段全部换成用户段
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iret                    ; 起飞 → Ring 3

; ============================================================
;  syscall_handler (int 0x80, IDT 门 DPL=3)
;
;  调用约定: eax=系统调用号, ebx/ecx/edx=参数, 返回值写回 eax
;  用户程序通过 user/lib.h 的内联汇编生成 int 0x80。
;
;  进入时 CPU 已切到 TSS.esp0 的内核栈, CS=0x08 (门的段选择子),
;  但 DS/ES 还是用户段 — 平坦模型下不换也能用, 这里换回内核段
;  是"规矩": 内核代码不该挂着 DPL3 的段描述符跑。
; ============================================================
global syscall_handler
syscall_handler:
    pusha                   ; 帧: [esp+0]=edi .. [esp+28]=eax
    mov eax, esp            ; eax = 帧指针
    push ds
    push es
    mov dx, 0x10
    mov ds, dx
    mov es, dx
    push eax                ; 参数 = 帧指针
    call syscall_dispatch   ; C 从帧里读 eax/ebx/ecx/edx, 返回值写帧的 eax 槽
    add esp, 4
    pop es
    pop ds
    popa                    ; eax ← 返回值
    iret                    ; 回用户态 (CPU 自动恢复用户 SS/ESP)
