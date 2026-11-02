; ============================================================
;  异常处理桩 (里程碑 10: Ring 3 用户态)
;
;  之前 IDT 的 0~31 号全是空门 — 用户态代码一崩就是
;  #GP → 三重故障 → 复位循环。现在每个异常都有桩。
;
;  栈帧布局 (进 exc_common 后, esp 从低到高):
;    +0  .. +31  8 个通用寄存器 (pusha 顺序: edi,esi,ebp,esp,ebx,edx,ecx,eax)
;    +32        向量号 (桩压入)
;    +36        错误码 (有错误码的异常由 CPU 压; 没有的桩压 0 占位)
;    +40        EIP
;    +44        CS
;    +48        EFLAGS
;    +52        ESP        (仅用户态异常: 特权级切换时 CPU 压的)
;    +56        SS         (同上)
;
;  有错误码的异常: 8 (#DF), 10 (#TS), 11 (#NP), 12 (#SS),
;                 13 (#GP), 14 (#PF), 17 (#AC)
;  其余 CPU 不压错误码 — 桩补一个 0, 让 C 端布局统一。
;
;  用户态异常: exc_dispatch 打印现场后 longjmp 回 shell
;  (杀掉进程), 不会回到这里; 内核态异常: panic, 停在 hlt 循环。
; ============================================================

[BITS 32]
section .text

extern exc_dispatch

%macro EXC_NOERR 1
global exc%1_handler
exc%1_handler:
    push 0          ; 补占位错误码 (帧布局与有错误码的异常统一)
    push %1         ; 向量号
    jmp exc_common
%endmacro

%macro EXC_ERR 1
global exc%1_handler
exc%1_handler:
    push %1         ; 向量号 (错误码 CPU 已经压在栈上)
    jmp exc_common
%endmacro

EXC_NOERR 0
EXC_NOERR 1
EXC_NOERR 2
EXC_NOERR 3
EXC_NOERR 4
EXC_NOERR 5
EXC_NOERR 6
EXC_NOERR 7
EXC_ERR   8
EXC_NOERR 9
EXC_ERR   10
EXC_ERR   11
EXC_ERR   12
EXC_ERR   13
EXC_ERR   14
EXC_NOERR 15
EXC_NOERR 16
EXC_ERR   17
EXC_NOERR 18
EXC_NOERR 19
%assign i 20
%rep 12
EXC_NOERR i
%assign i i+1
%endrep

exc_common:
    pusha                   ; +0..+31
    mov eax, esp            ; eax = 帧指针
    push eax                ; 参数
    call exc_dispatch       ; 用户态异常不会返回到这里 (longjmp 回 shell)
    add esp, 4
    popa
    add esp, 8              ; 丢向量号 + 错误码
    iret

; C 端查表用的桩地址数组 (idt.c 一次性装 32 个门)
global exc_stubs
exc_stubs:
%macro STUB_DD 1
    dd exc%1_handler
%endmacro
%assign i 0
%rep 32
STUB_DD i
%assign i i+1
%endrep
