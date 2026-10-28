; ============================================================
;  实模式往返的 32 位外壳 (Wrapper)
;
;  void rm_switch_mode(uint16_t mode);
;
;  工作流程:
;    1. cli — 整个往返期间禁止中断
;    2. 把内核状态存进低内存保存区 (0x5400, 跳板返回时读取)
;    3. 关分页 → 加载跳板的 GDT → 远跳到 0x18:0x6100 (跳板入口)
;    4. 跳板完成 INT 10h 后, 从 0x540A 保存的返回地址跳回来
;
;  保存区布局 (避开 SeaBIOS 实模式栈 0x7000):
;    0x5400: GDTR (6 字节: limit + base)
;    0x5406: CR3   (页目录地址)
;    0x540A: 返回地址 (跳板最后 jmp dword [0x540A] 回到这里)
;    0x540E: ESP   (内核栈指针)
;    0x5412: 视频模式号 (第一个参数, 跳板里 INT 10h 用)
;    0x5414: IDTR (6 字节: limit + base)
;
;  注意: 该函数不走 ret 返回 — 跳板直接跳回保存的返回地址,
;  对 C 调用者来说表现和普通函数完全一样。
;
;  调试: 串口打点 A(进外壳)/B(进跳板前), 跳板里打 C~G,
;  串口日志断在哪个字母就说明崩在哪个环节。
; ============================================================

[BITS 32]
section .text
global rm_switch_mode

; 串口调试输出一个字符 (BL=字符)
serial_out:
    mov dx, 0x3FD           ; 线路状态寄存器
.wait:
    in al, dx
    test al, 0x20           ; bit5 = 发送缓冲区空
    jz .wait
    mov al, bl
    mov dx, 0x3F8
    out dx, al
    ret

rm_switch_mode:
    cli                     ; 【关键】往返期间 IF=0, 任何 IRQ 都不会打扰

    mov bl, 'A'             ; 打点: 外壳启动, 开始保存状态
    call serial_out

    sgdt [0x5400]           ; 保存内核 GDT
    sidt [0x5414]           ; 保存内核 IDT (跳板里会被换成 BIOS 的, 返回时要恢复)
    mov eax, cr3
    mov [0x5406], eax       ; 保存页目录
    mov [0x540E], esp       ; 保存内核栈指针

    mov eax, [esp + 4]      ; 第一个参数: 视频模式号
    mov [0x5412], ax        ; 低 16 位就是模式号 (0x13 / 0x03)

    pop eax
    mov [0x540A], eax       ; 保存返回地址 (pop 后 esp 正好是返回后的栈顶)

    ; 关闭分页 (先关分页才能安全切 16 位模式)
    mov eax, cr0
    and eax, 0x7FFFFFFF
    mov cr0, eax
    jmp .flush_pg           ; 跳转刷新流水线

.flush_pg:
    ; 加载跳板自己的 GDT (0x6000 处是它的 GDTR 描述)
    lgdt [0x6000]

    mov bl, 'B'             ; 打点: 跳板 GDT 已加载
    call serial_out

    ; 远跳到 16 位代码段 0x18, 偏移 0x6100 (跳板入口)
    jmp 0x18:0x6100
