; ============================================================
;  switch_task — 协作式上下文切换 (与抢占式共享同一种帧格式)
;
;  帧格式 (44 字节, 低地址 → 高地址):
;    +0  .. +31   8 个通用寄存器 (popa 顺序)
;    +32 .. +35   EIP
;    +36 .. +39   CS
;    +40 .. +43   EFLAGS
;
;  恢复方 (定时器抢占桩 irq0_handler / 本函数) 统一用 popa + iret 还原,
;  两种切换路径产出的帧可以互相恢复。
; ============================================================
global switch_task

switch_task:
    mov eax, [esp + 4]          ; 当前任务
    mov edx, [esp + 8]          ; 下一个任务

    pusha                       ; 8 个寄存器 → 帧 +0..+31
    ; 此刻栈上恰好是: [esp+0..31]=8 寄存器, [esp+32..35]=返回地址(即 EIP 槽)
    mov dword [esp + 36], 0x08  ; CS = 内核代码段
    mov dword [esp + 40], 0x202 ; EFLAGS = IF=1 (yield 里 cli 后 IF=0, 恢复方
                                 ; 会在 yield 尾部 sti 重开中断, 硬编码 1 安全)

    mov [eax], esp              ; 保存当前任务帧指针
    mov esp, [edx]              ; 切到下一个任务的帧
    popa                        ; 恢复 8 个寄存器
    iret                        ; 跳到下一个任务的 EIP, 恢复 CS/EFLAGS
