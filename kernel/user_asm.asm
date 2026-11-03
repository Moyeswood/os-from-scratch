; ============================================================
;  用户态相关汇编
;
;  里程碑 10: gdt_reload_segments + enter_user + syscall_handler
;  里程碑 11b: enter_user 退役 — 用户进程现在由 proc.c 直接
;    构造 52 字节初始 Ring 3 帧 (EIP/CS/EFLAGS/ESP/SS) 交给
;    调度器, 首次被调度时 popa+iret 落进用户栈, 不再需要
;    "从内核调用链 iret 进 Ring 3"的 enter_user。
; ============================================================

[BITS 32]
section .text

extern tss              ; gdt.c 的全局 TSS (esp0 在偏移 4)
extern syscall_dispatch ; user.c: void syscall_dispatch(user_frame_t* frame)

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
;  syscall_handler (int 0x80, IDT 门 DPL=3)
;
;  调用约定: eax=系统调用号, ebx/ecx/edx=参数, 返回值写回 eax
;  用户程序通过 user/lib.h 的内联汇编生成 int 0x80。
;
;  进入时 CPU 已切到 TSS.esp0 的内核栈 (里程碑 11b: 每个用户
;  任务有自己的内核栈, 切换任务时调度器会同步 TSS.esp0),
;  CS=0x08 (门的段选择子), 但 DS/ES 还是用户段 — 平坦模型下
;  不换也能用, 这里换回内核段是"规矩": 内核代码不该挂着 DPL3
;  的段描述符跑。
;
;  帧布局 (pusha + iret 返回帧) 与 user.h 的 user_frame_t 一致:
;  [0]=edi .. [7]=eax, [8]=eip [9]=cs [10]=eflags [11]=esp_user [12]=ss_user
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
