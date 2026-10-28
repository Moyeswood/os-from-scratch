[BITS 32]
section .text
global rm_jump_to_trampoline

; 串口调试函数（直接操作寄存器）
serial_out:
    ; 等待发送缓冲区空
    mov dx, 0x3FD
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    ; 发送字符
    mov dx, 0x3F8
    mov al, cl
    out dx, al
    ret

rm_jump_to_trampoline:
    ; 输出 'A'
    mov cl, 'A'
    call serial_out

    ; 保存 CR3
    mov eax, cr3
    mov [0x6E04], eax

    ; 关分页
    mov eax, cr0
    and eax, 0x7FFFFFFF
    mov cr0, eax

    ; 输出 'B'
    mov cl, 'B'
    call serial_out

    ; 写 VGA
    mov ax, 0xB800
    mov es, ax
    mov di, 0
    mov byte [es:di], 'X'
    mov byte [es:di+1], 0x0F

    ; 输出 'C'
    mov cl, 'C'
    call serial_out

    ; 停机
    cli
    hlt
