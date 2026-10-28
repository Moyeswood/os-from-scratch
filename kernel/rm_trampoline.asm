; ============================================================
;  实模式跳板 (Trampoline)
;  被 rm_load_trampoline() 复制到物理地址 0x6000 后执行
;
;  内存布局 (所有地址都是 [ORG 0x6000] 算出的绝对地址):
;    0x6000  gdt_ptr    6 字节 GDTR 描述 (limit + base=0x6020)
;    0x6020  gdt        4 个段描述符 (16位代码/16位数据/32位代码/32位数据)
;    0x6100  entry16    16 位保护模式入口 (选择子 0x18)
;    ~0x6120 real_entry 真正的实模式代码 (INT 10h 在这里执行)
;    ~0x6150 entry32    32 位返回代码 (选择子 0x08)
;
;  为什么跳板必须放在低内存 (< 1MB)?
;  实模式 CS:IP 用 16 位偏移寻址, 最大只能表示 0xFFFF。
;  内核代码链接在 0x10000 以上, 若原地降级, 高地址会被截断。
;  所以把这一小段独立编译成二进制, 拷到 0x6000 执行。
;
;  与 rm_entry.asm 的约定 (保存区在 0x5400):
;    0x5400: 内核 GDTR (6 字节, 返回时恢复)
;    0x5406: 内核 CR3   (页目录, 返回时恢复)
;    0x540A: 返回地址   (最终跳回这里)
;    0x540E: 内核 ESP   (返回时恢复栈)
;    0x5412: 请求的视频模式号 (实模式里 INT 10h 用)
;    0x5414: 内核 IDTR (6 字节, 返回时恢复)
;
;  与 boot.asm 的约定 (低内存 0x580):
;    0x580: BIOS 的 GDTR (boot.asm 进保护模式前保存)
;    0x586: BIOS 的 IDTR
;    调 INT 10h 前必须恢复 BIOS 的这两张表 — SeaVGABIOS 切换
;    视频模式时会短暂进入保护模式, 依赖它们。
;
;  为什么保存区放 0x5400 而不是 0x6E00?
;  SeaBIOS 在实模式里把自己的栈放在物理地址 0x7000 (向下生长),
;  INT 10h 调用期间可能压到 0x6E00 一带, 会把保存区踩坏。
;  0x5400 远离 BIOS 栈 (0x7000) 和跳板 (0x6000), 安全。
;
;  中断安全: 整个往返期间 IF=0 (rm_entry.asm 开头 cli)。
;  PIC 只开放了 IRQ1, 键盘中断被挂起, 返回内核后自然处理。
;
;  调试: 每个环节向串口打点 (B~H), 串口日志断在哪个字母
;  就说明崩在哪个环节。
; ============================================================

[BITS 16]
[ORG 0x6000]

; ---------- GDTR 描述 (必须放在文件开头, 偏移 0) ----------
; rm_entry.asm 里执行 lgdt [0x6000] 读的就是这 6 个字节
gdt_ptr:
    dw gdt_end - gdt - 1        ; GDT 上限
    dd gdt                      ; GDT 物理地址 = 0x6020

    times 0x20 - ($ - $$) db 0  ; 填充到 0x6020

; ---------- GDT: 4 个段, 基地址全是 0 (实模式访问低内存) ----------
; 描述符结构 (8 字节): limit低16 | base低16 | base中8 | 访问字节 | limit高4+标志 | base高8
gdt:
    ; 0x00: 空描述符 (必须)
    dq 0
    ; 0x08: 32 位代码段 (基址 0, 上限 4GB) — 升回保护模式用
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b, 11001111b, 0x00
    ; 0x10: 32 位数据段 (基址 0, 上限 4GB)
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b, 11001111b, 0x00
    ; 0x18: 16 位代码段 (基址 0, 上限 64KB) — 降级过渡用
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b, 00001111b, 0x00
    ; 0x20: 16 位数据段 (基址 0, 上限 64KB)
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b, 00001111b, 0x00
gdt_end:

    times 0x100 - ($ - $$) db 0 ; 填充到 0x6100

; ---------- 第 1 站: 16 位保护模式入口 (CS=0x18, IP=0x6100) ----------
entry16:
    mov ax, 0x20            ; 16 位数据段
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x5FF0          ; 栈放在跳板下方 (0x5FF0 向下生长, 不碰 0x6000 的代码)

    mov bl, 'C'             ; 打点: 已进入 16 位保护模式
    call marker

    ; 【关键】清 CR0.PE → 正式进入实模式
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax

    ; 远跳转刷新 CS 为实模式段 (CS=0, IP=real_entry)
    jmp 0x0000:real_entry

; ---------- 第 2 站: 实模式代码 ----------
real_entry:
    xor ax, ax              ; 实模式段 = 0 (物理地址 = 段<<4 + 偏移)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x5FF0

    mov bl, 'D'             ; 打点: 已进入实模式
    call marker

    ; 【关键】恢复 BIOS 自己的 GDT/IDT (boot.asm 保存在 0x580/0x586)
    ; SeaVGABIOS 设置视频模式时会短暂进入保护模式,
    ; 如果 GDTR/IDTR 还是跳板/内核的表, 它会在错误的内存里乱写
    lgdt [0x580]
    lidt [0x586]

    ; 读取请求的模式号 (rm_entry.asm 写入 0x5412)
    ; INT 10h AH=00h AL=模式:  0x13 = 320x200x256 色图形, 0x03 = 80x25 文本
    mov ax, [0x5412]
    int 0x10

    mov bl, 'E'             ; 打点: BIOS 调用完成
    call marker

    ; ---------- 升回保护模式 ----------
    ; 换回跳板自己的 GDT (下面远跳 0x08:entry32 要用它的 32 位代码段)
    lgdt [0x6000]

    ; A20 引导时已开启, 保险起见再开一次 (不然高地址访问会绕回低内存)
    in al, 0x92
    or al, 2
    out 0x92, al

    mov eax, cr0
    or al, 1                ; 置 CR0.PE → 进入保护模式
    mov cr0, eax

    ; 远跳转: CS=0x08 (32 位代码段), 刷新流水线
    jmp 0x08:entry32

; ---------- 第 3 站: 32 位返回代码 ----------
[BITS 32]
entry32:
    mov ax, 0x10            ; 32 位数据段
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov bl, 'F'             ; 打点: 已升回 32 位保护模式
    mov dx, 0x3FD
.f1:
    in al, dx
    test al, 0x20
    jz .f1
    mov al, bl
    mov dx, 0x3F8
    out dx, al

    ; 恢复内核原来的 GDT (rm_entry.asm 保存在 0x5400)
    lgdt [0x5400]

    ; 恢复内核的 IDT (rm_entry.asm 保存在 0x5414)
    ; 不恢复的话, 返回内核开中断后 IRQ1 会走 BIOS 的 IDT = 崩溃
    lidt [0x5414]

    ; 用内核代码段 (0x08) 刷新 CS — 内核 GDT 和跳板 GDT 的选择子布局相同
    jmp 0x08:.reload

.reload:
    ; GDT 已换回内核的, 再加载一遍数据段 (选择子 0x10 含义相同)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov bl, 'G'             ; 打点: GDT 已恢复
    mov dx, 0x3FD
.g1:
    in al, dx
    test al, 0x20
    jz .g1
    mov al, bl
    mov dx, 0x3F8
    out dx, al

    ; 恢复 CR3 并重新开启分页
    mov eax, [0x5406]
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    jmp .flush              ; 跳转刷新流水线, 让分页生效

.flush:
    ; 恢复内核栈、开中断, 跳回 rm_entry.asm 保存的返回地址
    mov esp, [0x540E]
    sti
    jmp dword [0x540A]

; ---------- 调试用: 串口输出一个字符 (BL=字符) ----------
; 放在文件末尾, 不影响 entry16 必须位于 0x6100 的布局
[BITS 16]
marker:
    mov dx, 0x3FD           ; 线路状态寄存器
.loop:
    in al, dx
    test al, 0x20           ; bit5 = 发送缓冲区空
    jz .loop
    mov al, bl
    mov dx, 0x3F8           ; 数据寄存器
    out dx, al
    ret
