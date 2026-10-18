[BITS 16]
[ORG 0x7C00]

xor ax, ax
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7C00

in al, 0x92
or al, 2
out 0x92, al

mov ah, 0x02
mov al, 10        
mov ch, 0
mov cl, 2         
mov dh, 0
mov bx, 0x1000
mov es, bx
xor bx, bx
int 0x13
jc disk_error     

cli
lgdt [gdt_desc]
mov eax, cr0
or eax, 1
mov cr0, eax

jmp dword 0x08:pmode

disk_error:
    mov ah, 0x0E
    mov al, 'E'
    int 0x10
    jmp $

[BITS 32]
pmode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000  
    jmp 0x10000

gdt_start:
    dq 0                  
gdt_code:
    dw 0xFFFF             
    dw 0x0000             
    db 0x00
    db 10011010b          
    db 11001111b          
    db 0x00               
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b          
    db 11001111b
    db 0x00
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 510 - ($ - $$) db 0
dw 0xAA55
