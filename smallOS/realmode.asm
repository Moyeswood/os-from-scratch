[BITS 32]
section .text

global test_realmode

; 保存保护模式状态
section .data
saved_cr3:    dd 0
saved_esp:    dd 0

section .text
test_realmode:
    ; 保存当前状态
    mov [saved_esp], esp
    mov eax, cr3
    mov [saved_cr3], eax
    
    ; 关闭中断
    cli
    
    ; 关闭分页
    mov eax, cr0
    and eax, 0x7FFFFFFF
    mov cr0, eax
    
    ; 加载 16 位 GDT
    lgdt [gdt16_desc]
    
    ; 跳转到 16 位代码段（选择子 0x18）
    jmp 0x18:CODE16

[BITS 16]
CODE16:
    ; 设置 16 位数据段（选择子 0x20）
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; 设置实模式栈
    mov sp, 0x7C00
    
    ; 清除 CR0.PE，进入实模式
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    
    ; 用绝对地址跳转到实模式代码
    ; 先计算 REAL16 的物理地址
    mov eax, REAL16
    and eax, 0xFFFF
    push eax
    retf

[BITS 16]
REAL16:
    ; 设置段寄存器为 0
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; 现在在真正的实模式了！
    ; 用 BIOS INT 10h 在屏幕打印 'R'
    mov ah, 0x0E
    mov al, 'R'
    int 0x10
    
    ; 延时
    mov cx, 0xFFFF
.delay:
    nop
    loop .delay
    
    ; 准备返回保护模式
    ; 开启 A20
    in al, 0x92
    or al, 2
    out 0x92, al
    
    ; 设置 CR0.PE
    mov eax, cr0
    or al, 1
    mov cr0, eax
    
    ; 用绝对地址跳转到 32 位代码
    mov eax, CODE32
    and eax, 0xFFFF
    push 0x08
    push eax
    retf

[BITS 32]
CODE32:
    ; 设置 32 位数据段（选择子 0x10）
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; 恢复栈
    mov esp, [saved_esp]
    
    ; 恢复 CR3
    mov eax, [saved_cr3]
    mov cr3, eax
    
    ; 重新开启分页
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    
    ; 重新开启中断
    sti
    
    ; 返回
    ret

; 16 位 GDT
align 16
gdt16:
    dq 0
gdt16_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 00001111b
    db 0x00
gdt16_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 00001111b
    db 0x00
gdt16_end:

gdt16_desc:
    dw gdt16_end - gdt16 - 1
    dd gdt16
