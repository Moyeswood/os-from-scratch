[BITS 16]
[ORG 0x6000]

gdt_ptr:
    dw  gdt_end - gdt - 1
    dd  gdt
    times 0x20 - ($ - $$) db 0

gdt:
    dq  0
    dw  0xFFFF, 0x0000, 0x00
    db  10011010b, 11001111b, 0x00
    dw  0xFFFF, 0x0000, 0x00
    db  10010010b, 11001111b, 0x00
    dw  0xFFFF, 0x0000, 0x00
    db  10011010b, 00001111b, 0x00
    dw  0xFFFF, 0x0000, 0x00
    db  10010010b, 00001111b, 0x00
gdt_end:
    times 0x100 - ($ - $$) db 0

entry:
    ; 第一步：立即写 VGA，证明跳板开始执行了
    mov ax, 0xB800
    mov es, ax
    mov di, 0          ; 屏幕左上角
    
    ; 写 "X"
    mov byte [es:di], 'X'
    mov byte [es:di+1], 0x0F
    
    ; 立即停机，不返回
    cli
    hlt
