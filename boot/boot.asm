[BITS 16]
[ORG 0x7C00]

xor ax, ax
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7C00

; ★必须先保存 BIOS 传入的启动驱动器号。后面的 INT 15h 不保证保留 DL。
mov [0x600], dl

; ================= 兼容旧内存探测 (E820 失败时回退) =================
mov ah, 0x88
int 0x15
mov [0x500], ax
; ==================================================

; ================= 获取 BIOS E820 内存地图 =================
; 记录区放在 0x2000, 最多 32 项, 每项 24 字节。
; 0x4000 保存实际项数。该区域不碰启动扇区、BIOS EBDA、
; 实模式跳板 (0x6000) 和内核加载区 (0x10000)。
;
; E820 项格式:
;   uint64 base, uint64 length, uint32 type, uint32 acpi_attributes
; type=1 表示可用内存。
    xor edi, edi
    mov di, 0x2000
    xor eax, eax
    mov ecx, 32 * 6
    rep stosd

    xor edi, edi
    mov di, 0x2000
    xor ebx, ebx
    xor bp, bp
.e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150       ; "SMAP"
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    cmp ecx, 20
    jb .e820_next

    cmp dword [di + 16], 0    ; type=0 的项无效
    je .e820_next
    inc bp
    add di, 24
    cmp bp, 32
    jae .e820_done
.e820_next:
    test ebx, ebx
    jnz .e820_loop
.e820_done:
    test bp, bp
    jnz .e820_store

    ; 极端兼容回退: 根据 int 0x15 AX=0x88 构造 1MB 以上可用区。
    movzx eax, word [0x500]
    shl eax, 10
    mov dword [0x2000], 0x00100000
    mov dword [0x2004], 0
    mov dword [0x2008], eax
    mov dword [0x200C], 0
    mov dword [0x2010], 1
    mov dword [0x2014], 0
    mov bp, 1
.e820_store:
    mov [0x4000], bp
    ; INT 15h 可能修改段寄存器, 读盘前恢复 BIOS 约定的 0 基址。
    xor ax, ax
    mov ds, ax
    mov es, ax
; ==========================================================

; 【关键】保存 BIOS 的 GDTR/IDTR 到 0x580/0x586
; 以后内核要临时回实模式调 BIOS (INT 10h) 时,
; 必须先恢复 BIOS 自己的 GDT/IDT — SeaVGABIOS 切换模式时
; 会短暂进入保护模式, 依赖这些表
sgdt [0x580]
sidt [0x586]

; 开启 A20 地址线
in al, 0x92
or al, 2
out 0x92, al

; ============ 循环读盘 (每次 1 个扇区, 读 300 次 = 150KB) ============
; ★内核镜像必须小于 150KB — 加新模块时注意 kernel.bin 体积!
; 2026-11-02: 里程碑 10 (Ring 3 用户态) 加 gdt/tss/异常处理/6 个
; 内嵌用户程序, kernel.bin 破 100KB → 从 200 扇区扩到 300 扇区
mov ch, 0           ; 柱面 0
mov cl, 2           ; 起始扇区 2 (CL 高 2 位为 0，因为柱面 0)
mov dh, 0           ; 磁头 0
mov bx, 0x1000      ; 目标内存段地址
mov es, bx
xor bx, bx          ; 目标内存偏移地址

mov si, 300         ; 循环计数器 (300 个扇区 = 150KB)

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
