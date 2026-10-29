[BITS 16]
[ORG 0x7C00]

xor ax, ax
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7C00

; ================= 获取扩展内存大小 =================
mov ah, 0x88
int 0x15
mov [0x500], ax
; ==================================================

; 【关键】保存 BIOS 的 GDTR/IDTR 到 0x580/0x586
; 以后内核要临时回实模式调 BIOS (INT 10h) 时,
; 必须先恢复 BIOS 自己的 GDT/IDT — SeaVGABIOS 切换模式时
; 会短暂进入保护模式, 依赖这些表
sgdt [0x580]
sidt [0x586]

; 保存启动驱动器号 (DL) 到安全地址 0x600
mov [0x600], dl

; 开启 A20 地址线
in al, 0x92
or al, 2
out 0x92, al

; ============ 循环读盘 (每次 1 个扇区, 读 200 次 = 100KB) ============
; ★内核镜像必须小于 100KB — 加新模块 (fs/编辑器/编译器) 时注意
; kernel.bin 体积! 2026-10-29: 加 fs 后 58KB, 50KB 上限不够 → 扩到 100KB
mov ch, 0           ; 柱面 0
mov cl, 2           ; 起始扇区 2 (CL 高 2 位为 0，因为柱面 0)
mov dh, 0           ; 磁头 0
mov bx, 0x1000      ; 目标内存段地址
mov es, bx
xor bx, bx          ; 目标内存偏移地址

mov si, 200         ; 循环计数器 (200 个扇区 = 100KB)

.read_sector:
    ; 【关键修复 1】：每次循环前必须重新设置这些寄存器！
    mov ah, 0x02    ; 功能号：读扇区
    mov al, 1       ; 读取扇区数：1
    mov dl, [0x600] ; 恢复驱动器号
    
    int 0x13        ; 调用 BIOS 读盘
    jc disk_error   ; 如果失败，跳转到错误处理
    
    ; 【关键修复 2】：正确更新 CHS 地址，严格分离扇区号和柱面位
    mov al, cl
    and al, 0x3F    ; 提取当前扇区号 (CL 的低 6 位)
    inc al          ; 扇区号 +1
    cmp al, 19      ; 如果扇区号 > 18
    jne .update_cl  ; 没超过，直接更新 CL
    
    ; 超过 18，扇区号重置为 1，磁头号 +1
    mov al, 1       ; 新扇区号 = 1
    and cl, 0xC0    ; 保留 CL 的高 2 位 (柱面高 2 位)
    or cl, al       ; 组合新的扇区号到 CL
    
    inc dh          ; 磁头号 +1
    cmp dh, 2       ; 如果磁头号 >= 2
    jne .next
    mov dh, 0       ; 磁头号重置为 0
    inc ch          ; 柱面号 +1
    jmp .next

.update_cl:
    and cl, 0xC0    ; 保留 CL 的高 2 位
    or cl, al       ; 组合新的扇区号到 CL

.next:
    add bx, 512         ; 内存地址 +512
    jnc .no_wrap        ; ★BX 是 16 位: 超过 64KB 会回绕并覆盖内核头部!
    mov ax, es          ;   128 个扇区 = 64KB → 推进段寄存器 0x1000
    add ax, 0x1000
    mov es, ax
    xor bx, bx
.no_wrap:
    dec si              ; 计数器 -1
    jnz .read_sector ; 如果没读完，继续循环
; ==============================================================================

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
