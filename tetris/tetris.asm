; ============================================================
;  裸机俄罗斯方块  (x86 16 位实模式, NASM 语法)
;
;  构建: nasm -f bin tetris.asm -o tetris.img
;  运行: qemu-system-i386 -drive format=raw,file=tetris.img
;
;  结构:
;    第 1 扇区 (512 字节) : 引导扇区, 由 BIOS 加载到 0x7C00,
;                           负责把后面的扇区读进内存 0x7E00 并跳过去
;    第 2.. 扇区           : 游戏主体 (stage2)
;
;  用到的 BIOS 服务:
;    INT 10h  显示   (AX=0013h 切到 320x200x256 色图形模式, 0Eh 打印字符)
;    INT 13h  磁盘   (AH=02h 读扇区)
;    INT 16h  键盘   (AH=01h 查询是否有按键, AH=00h 读取按键)
;    INT 1Ah  时钟   (AH=00h 读取系统滴答计数, 约 18.2 次/秒)
; ============================================================

[bits 16]
[org 0x7C00]

; ---------- 常量 ----------
BOARD_W   equ 10            ; 棋盘宽 (格)
BOARD_H   equ 20            ; 棋盘高 (格)
BOARD_X   equ 120           ; 棋盘左上角像素坐标
BOARD_Y   equ 20
LOCK_DELAY  equ 7          ; 锁定延迟 (滴答数, 约 0.4 秒)
MAX_RESETS  equ 15         ; 每个方块最多允许重置锁定计时的次数 (防止无限拖延)
SECTORS   equ 16           ; 要从磁盘读入的扇区数 (16*512=8KB; 单次读取别超过 17, 否则可能跨磁道失败)

; ============================================================
;  第 1 扇区: 引导程序
; ============================================================
boot:
    cli
    xor ax, ax
    mov ds, ax              ; 数据段/附加段/栈段全部设为 0
    mov es, ax              ; 这样 [label] 就是物理地址 0000:label
    mov ss, ax
    mov sp, 0x7C00          ; 栈从引导扇区下方向下长
    sti
    cld
    mov [boot_drive], dl    ; BIOS 把启动盘号放在 DL

    mov si, 3               ; 最多重试 3 次
.read:
    mov ah, 0x02            ; INT 13h/02h: 读扇区 (CHS 方式)
    mov al, SECTORS         ; 读几个扇区
    mov ch, 0               ; 柱面 0
    mov cl, 2               ; 扇区号从 1 开始, 1 号是引导扇区自己, 所以从 2 开始
    mov dh, 0               ; 磁头 0
    mov dl, [boot_drive]
    mov bx, 0x7E00          ; 目标: ES:BX = 0000:7E00
    int 0x13
    jc .retry
    jmp stage2
.retry:
    xor ah, ah              ; INT 13h/00h: 复位磁盘
    mov dl, [boot_drive]
    int 0x13
    dec si
    jnz .read

    mov si, msg_err         ; 读盘失败: 打印错误并停机
.p: lodsb
    test al, al
    jz .hang
    mov ah, 0x0E
    int 0x10
    jmp .p
.hang:
    cli
    hlt
    jmp .hang

boot_drive db 0
msg_err    db 'Disk error', 0

times 510-($-$$) db 0       ; 补零到 510 字节
dw 0xAA55                   ; 引导扇区魔数, BIOS 靠它判断这是可启动盘

; ============================================================
;  第 2 扇区起: 游戏主体
; ============================================================
stage2:
    mov ax, 0x0013          ; 320x200, 256 色; 显存在 0xA000:0000, 1 字节 = 1 像素
    int 0x10
    call apply_palette      ; 先按等级 1 设置调色板 (标题画面也要用)
    call title_screen       ; 标题画面, 按任意键继续
    call draw_frame
    call init_game

main_loop:
    cmp byte [dirty], 0
    je .nodraw
    call draw_all
    mov byte [dirty], 0
.nodraw:
    hlt                     ; 睡到下一个中断(定时器/键盘)到来, 省 CPU

.key:                       ; 把键盘缓冲区里的按键全部处理掉
    mov ah, 1
    int 0x16
    jz .nokey               ; ZF=1 表示没有按键
    xor ah, ah
    int 0x16                ; AH=扫描码, AL=ASCII
    call handle_key
    jmp .key
.nokey:
    mov ah, 0
    int 0x1A                ; CX:DX = 滴答计数
    call beep_service       ; 蜂鸣响满时间就关扬声器
    mov al, [paused]
    or  al, [gameover]
    jnz .idle
    call lock_check         ; 落地后的锁定计时 (DX = 当前滴答, 会被保留)
    mov ax, dx
    sub ax, [last_tick]     ; 距上次下落过了多少滴答
    mov bl, 11
    sub bl, [level]         ; 间隔 = 11 - 等级 (等级越高越快)
    xor bh, bh
    cmp ax, bx
    jb  main_loop
    mov [last_tick], dx
    call gravity
    jmp main_loop
.idle:
    mov [last_tick], dx     ; 暂停/结束期间不累计时间
    mov [lock_start], dx    ; 暂停期间锁定计时也不累计
    jmp main_loop

; ------------------------------------------------------------
;  键盘处理 (AH = 扫描码)
; ------------------------------------------------------------
handle_key:
    cmp ah, 0x01            ; ESC: 通过键盘控制器复位整机
    jne .n1
    mov al, 0xFE
    out 0x64, al
.n1:
    cmp ah, 0x13            ; R: 重新开始
    jne .n2
    call init_game
    ret
.n2:
    cmp ah, 0x32            ; M: 静音开关
    jne .n2b
    xor byte [mute], 1
    ret
.n2b:
    cmp byte [gameover], 1
    je  .ret
    cmp ah, 0x19            ; P: 暂停
    jne .n3
    xor byte [paused], 1
    mov byte [dirty], 1
    ret
.n3:
    cmp byte [paused], 0
    jne .ret
    cmp ah, 0x2E            ; C: 暂存
    je  do_hold
    cmp ah, 0x4B            ; 左 / A
    je  .left
    cmp ah, 0x1E
    je  .left
    cmp ah, 0x4D            ; 右 / D
    je  .right
    cmp ah, 0x20
    je  .right
    cmp ah, 0x48            ; 上 / W : 旋转
    je  .rot
    cmp ah, 0x11
    je  .rot
    cmp ah, 0x50            ; 下 / S : 软降
    je  .down
    cmp ah, 0x1F
    je  .down
    cmp ah, 0x39            ; 空格: 直落
    je  .drop
.ret:
    ret
.left:
    mov si, -1
    xor di, di
    jmp try_move
.right:
    mov si, 1
    xor di, di
    jmp try_move
.rot:
    jmp rotate
.down:
    xor si, si
    mov di, 1
    call try_move
    jc  .ret
    add word [score], 1     ; 软降每格 +1 分 (32 位: 低 16 位相加, 进位加到高 16 位)
    adc word [score+2], 0
    ret
.drop:
    jmp hard_drop

; ------------------------------------------------------------
;  游戏逻辑
; ------------------------------------------------------------
; 初始化/重新开始
init_game:
    mov di, board
    mov cx, BOARD_W*BOARD_H
    xor al, al
    rep stosb               ; 清空棋盘
    xor ax, ax
    mov [score], ax
    mov [score+2], ax
    mov [lines], ax
    mov [paused], al
    mov [gameover], al
    mov byte [level], 1
    mov byte [hold_piece], 0xFF   ; 0xFF = 暂存区为空
    mov byte [hold_used], 0
    xor ah, ah
    int 0x1A                ; 用时钟计数做随机数种子
    mov [last_tick], dx
    xor [seed], dx
    call bag_next
    mov [next_piece], al
    call spawn
    call apply_palette      ; 按等级 1 设置配色
    mov byte [dirty], 1
    ret

; 生成新方块 (当前 = next, 再随机一个 next); 出生点就碰撞则游戏结束
spawn:
    mov al, [next_piece]
    mov [cur_piece], al
    call bag_next
    mov [next_piece], al
spawn_cur:                  ; 把 cur_piece 摆到出生点 (暂存换出时也走这里)
    mov byte [grounded], 0  ; 新方块: 清掉锁定相关状态
    mov byte [lock_resets], 0
    mov byte [reset_req], 0
    mov byte [cur_rot], 0
    mov word [cur_x], 3
    mov word [cur_y], 0
    mov word [t_x], 3
    mov word [t_y], 0
    mov byte [t_rot], 0
    call collide
    jnc .ok
    mov byte [gameover], 1
    mov ax, 10847           ; 110 Hz 低音: 游戏结束
    call beep
.ok:
    mov byte [dirty], 1
    ret

; 随机数: 输入 CX=n, 输出 AL 属于 [0, n). 线性同余: seed = seed*25173 + 13849 (16 位)
random_mod:
    push cx
    push dx
    mov ax, [seed]
    mov dx, 25173
    mul dx
    add ax, 13849
    mov [seed], ax
    mov al, ah              ; 取高字节(低位的随机性较差)
    xor ah, ah
    xor dx, dx
    div cx                  ; DX = AX mod n
    mov al, dl
    pop dx
    pop cx
    ret

; 7-bag: 取下一个方块号 -> AL. 袋子用完(bag_pos>=7)就重新装满并洗牌
bag_next:
    push bx
    cmp byte [bag_pos], 7
    jb  .ok
    call refill_bag
.ok:
    mov bl, [bag_pos]
    xor bh, bh
    mov al, [bag+bx]
    inc byte [bag_pos]
    pop bx
    ret

; 装满 0..6 并用 Fisher-Yates 洗牌
refill_bag:
    push ax
    push bx
    push cx
    push si
    xor bx, bx
.fill:
    mov [bag+bx], bl
    inc bx
    cmp bx, 7
    jb  .fill
    mov bx, 6               ; i 从 6 倒数到 1
.shuf:
    lea cx, [bx+1]          ; n = i+1
    call random_mod         ; AL = j, 0 <= j <= i
    xor ah, ah
    mov si, ax
    mov al, [bag+bx]
    xchg al, [bag+si]       ; 交换 bag[i] 与 bag[j]
    mov [bag+bx], al
    dec bx
    jnz .shuf
    mov byte [bag_pos], 0
    pop si
    pop cx
    pop bx
    pop ax
    ret

; 取方块形状. 输入 BL=方块号, BH=旋转(0-3); 输出 AX = 4x4 位图
; 位图最高位是第 0 行第 0 列, 逐行向低位排列
get_mask:
    push bx
    mov al, bl
    shl al, 2
    add al, bh              ; 下标 = 方块号*4 + 旋转
    xor ah, ah
    shl ax, 1               ; 每项 2 字节
    mov bx, ax
    mov ax, [pieces+bx]
    pop bx
    ret

; 碰撞检测: 用 (t_x, t_y, t_rot) 摆放 cur_piece. CF=1 表示撞了(越界或压到已有方块)
collide:
    push ax
    push bx
    push cx
    push dx
    mov bl, [cur_piece]
    mov bh, [t_rot]
    call get_mask
    mov dx, ax              ; DX = 位图
    xor cx, cx              ; CL = 0..15 格子编号
.loop:
    test dx, 0x8000
    jz  .next
    mov al, cl
    shr al, 2               ; 行 = 编号 / 4
    xor ah, ah
    add ax, [t_y]
    mov bx, ax              ; BX = 棋盘行
    mov al, cl
    and al, 3               ; 列 = 编号 % 4
    xor ah, ah
    add ax, [t_x]           ; AX = 棋盘列
    cmp ax, BOARD_W
    jae .hit                ; 无符号比较: 负数也会被当成很大 -> 越界
    cmp bx, BOARD_H
    jae .hit
    imul bx, bx, BOARD_W
    add bx, ax
    cmp byte [board+bx], 0
    jne .hit
.next:
    shl dx, 1
    inc cl
    cmp cl, 16
    jb  .loop
    clc
    jmp .done
.hit:
    stc
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; 把当前方块写进棋盘 (值 = 方块号+1, 0 表示空)
lock_piece:
    push ax
    push bx
    push cx
    push dx
    mov bl, [cur_piece]
    mov bh, [cur_rot]
    call get_mask
    mov dx, ax
    xor cx, cx
.loop:
    test dx, 0x8000
    jz  .next
    mov al, cl
    shr al, 2
    xor ah, ah
    add ax, [cur_y]
    mov bx, ax
    mov al, cl
    and al, 3
    xor ah, ah
    add ax, [cur_x]
    imul bx, bx, BOARD_W
    add bx, ax
    mov al, [cur_piece]
    inc al
    mov [board+bx], al
.next:
    shl dx, 1
    inc cl
    cmp cl, 16
    jb  .loop
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; 尝试平移. 输入 SI=dx, DI=dy. 成功 CF=0, 被挡 CF=1
try_move:
    mov ax, [cur_x]
    add ax, si
    mov [t_x], ax
    mov ax, [cur_y]
    add ax, di
    mov [t_y], ax
    mov al, [cur_rot]
    mov [t_rot], al
    call collide
    jc  .blocked
    mov ax, [t_x]
    mov [cur_x], ax
    mov ax, [t_y]
    mov [cur_y], ax
    mov byte [dirty], 1
    mov byte [reset_req], 1 ; 成功移动过: 若已落地, 锁定计时可以重置
    clc
    ret
.blocked:
    stc
    ret

; 旋转 (顺时针). 撞墙时依次尝试左右偏移 0, +1, -1, +2, -2 (简易 wall kick)
rotate:
    mov al, [cur_rot]
    inc al
    and al, 3
    mov [t_rot], al
    xor si, si
.try:
    mov al, [kicks+si]
    cbw                     ; 带符号扩展 AL -> AX
    add ax, [cur_x]
    mov [t_x], ax
    mov ax, [cur_y]
    mov [t_y], ax
    call collide
    jnc .ok
    inc si
    cmp si, 5
    jb  .try
    ret
.ok:
    mov al, [t_rot]
    mov [cur_rot], al
    mov ax, [t_x]
    mov [cur_x], ax
    mov byte [dirty], 1
    mov byte [reset_req], 1 ; 旋转成功也请求重置
    ret

; 直落到底并锁定
hard_drop:
    xor si, si
    mov di, 1
.l: call try_move
    jc  .done
    add word [score], 2
    adc word [score+2], 0
    jmp .l
.done:
    jmp lock_and_next

; 定时下落一格, 落不下去就锁定
gravity:
    xor si, si
    mov di, 1
    call try_move           ; 落不下去也不立刻锁定, 交给 lock_check 的计时
    ret

; 脚下是否已经没有空间? CF=1 表示方块已落地(再往下一格就会碰撞)
on_ground:
    push ax
    mov ax, [cur_x]
    mov [t_x], ax
    mov ax, [cur_y]
    inc ax
    mov [t_y], ax
    mov al, [cur_rot]
    mov [t_rot], al
    call collide            ; 返回 CF
    pop ax                  ; pop 不影响标志位, CF 保留
    ret

; 锁定延迟. 输入 DX = 当前滴答 (保留).
;   刚落地        -> 开始计时
;   落地后移动/旋转 -> 重置计时 (每块最多 MAX_RESETS 次)
;   计时满 LOCK_DELAY -> 真正锁定
;   脚下又有空间    -> 取消落地状态, 继续受重力
lock_check:
    push ax
    push dx
    call on_ground
    jc  .grounded
    cmp byte [grounded], 0
    je  .done
    mov byte [grounded], 0  ; 脚下又有空间了: 恢复亮色
    mov byte [dirty], 1
    jmp .done
.grounded:
    cmp byte [grounded], 0
    jne .already
    mov byte [grounded], 1  ; 刚刚落地
    mov byte [dirty], 1     ; 变暗
    mov [lock_start], dx
    jmp .done
.already:
    cmp byte [reset_req], 0
    je  .timer
    cmp byte [lock_resets], MAX_RESETS
    jae .timer
    inc byte [lock_resets]
    mov [lock_start], dx    ; 最后一刻滑动: 重新计时
    jmp .done
.timer:
    mov ax, dx
    sub ax, [lock_start]
    cmp ax, LOCK_DELAY
    jb  .done
    call lock_and_next
.done:
    mov byte [reset_req], 0
    pop dx
    pop ax
    ret

lock_and_next:
    call lock_piece
    mov ax, 5423            ; 220 Hz: 落地
    call beep
    call flash_lines        ; 有满行就先闪烁
    call clear_lines
    call spawn
    mov byte [hold_used], 0 ; 新方块出现, 允许再次暂存
    ret

; 暂存 / 换出. 当前方块存入暂存区; 暂存区原有方块则换回, 为空则取下一个
do_hold:
    cmp byte [hold_used], 1
    je  .ret
    mov byte [hold_used], 1
    mov al, [cur_piece]
    mov bl, [hold_piece]
    mov [hold_piece], al
    cmp bl, 0xFF
    jne .swap
    call spawn
    jmp .done
.swap:
    mov [cur_piece], bl
    call spawn_cur
.done:
    mov ax, 1808            ; 660 Hz
    call beep
    mov byte [dirty], 1
.ret:
    ret

; PC 扬声器发声. 输入 AX = PIT 分频值 (频率 = 1193182 / AX). 响约 0.1 秒, 由主循环关闭
beep:
    cmp byte [mute], 1
    je  .ret
    push ax
    push cx
    push dx
    mov cx, ax              ; 暂存分频值
    mov al, 0xB6            ; 控制字: 通道2, 先低后高字节, 方波模式
    out 0x43, al
    mov al, cl
    out 0x42, al            ; 分频值低字节
    mov al, ch
    out 0x42, al            ; 分频值高字节
    in  al, 0x61
    or  al, 3               ; bit0=通道2门控, bit1=扬声器使能
    out 0x61, al
    xor ah, ah
    int 0x1A
    mov [beep_start], dx    ; 记下开始时刻
    mov byte [beep_on], 1
    pop dx
    pop cx
    pop ax
.ret:
    ret

; 消行: 从最下面一行往上扫, 满行就把上面整体下移一行, 然后重查同一行
clear_lines:
    xor dx, dx              ; DX = 本次消除行数
    mov bx, BOARD_H-1       ; BX = 当前检查的行
.row:
    imul si, bx, BOARD_W
    add si, board           ; SI = 该行起始地址
    mov cx, BOARD_W
.chk:
    lodsb
    test al, al
    jz  .notfull            ; 有空格, 不是满行
    loop .chk
    ; 满行: SI 现在指向该行末尾+1
    inc dx
    lea di, [si-1]          ; 目的: 该行最后一格
    mov cx, si
    sub cx, board+BOARD_W   ; 要搬的字节数 = 该行之上所有格子
    sub si, BOARD_W+1       ; 源: 上一行最后一格
    std                     ; 方向标志置位, 从后往前复制(避免覆盖)
    rep movsb
    cld
    mov di, board           ; 最上面一行清零
    mov cx, BOARD_W
    xor al, al
    rep stosb
    jmp .row                ; 同一行重新检查
.notfull:
    dec bx
    jns .row

    test dx, dx
    jz  .ret
    add [lines], dx
    mov bx, dx
    shl bx, 1
    mov ax, [score_tbl+bx]  ; 1/2/3/4 行 = 100/300/500/800
    mov cl, [level]
    xor ch, ch
    mul cx                  ; DX:AX = AX * 等级 (32 位结果)
    add [score], ax
    adc [score+2], dx
    mov ax, [lines]         ; 每消 10 行升一级, 上限 10
    xor dx, dx
    mov cx, 10
    div cx
    inc ax
    cmp ax, 10
    jbe .setlv
    mov ax, 10
.setlv:
    cmp al, [level]
    je  .ret                ; 等级没变, 不用动调色板
    mov [level], al
    call apply_palette      ; 升级: 换一套配色和边框色
    mov ax, 905             ; 1318 Hz: 升级提示音
    call beep
.ret:
    ret

; ------------------------------------------------------------
;  绘图 (显存 0xA000:0000, 每像素 1 字节, 行宽 320)
; ------------------------------------------------------------
; 填充矩形. AX=x, BX=y, CL=宽, CH=高, DL=颜色. 要求 ES=0xA000. 不破坏任何寄存器
fill_rect:
    push ax
    push bx
    push cx
    push dx
    push di
    imul di, bx, 320
    add di, ax              ; DI = y*320 + x
    mov al, dl              ; AL = 颜色
    mov dl, ch              ; DL = 剩余行数
    xor ch, ch              ; CX = 宽
.row:
    push cx
    push di
    rep stosb
    pop di
    pop cx
    add di, 320
    dec dl
    jnz .row
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; 画一个格子 (7x7, 留 1 像素缝). BL=列, BH=行 (相对棋盘左上角), DL=棋盘值(0..7)
draw_cell:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, dl
    xor ah, ah
    mov si, ax
    mov dl, [colors+si]     ; 棋盘值 -> VGA 调色板颜色
    mov al, bl
    xor ah, ah
    shl ax, 3
    add ax, BOARD_X         ; AX = 像素 x
    mov cl, bh
    xor ch, ch
    mov bx, cx
    shl bx, 3
    add bx, BOARD_Y         ; BX = 像素 y
    mov cx, 0x0707          ; 高 7, 宽 7
    call fill_rect
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; 画棋盘边框
draw_frame:
    push es
    mov ax, 0xA000
    mov es, ax
    mov dl, 9               ; 边框色: 调色板第 9 项, 随等级变化
    mov ax, BOARD_X-2
    mov bx, BOARD_Y-2
    mov cx, 0x0254          ; 上边: 宽 84 高 2
    call fill_rect
    mov bx, BOARD_Y+160
    call fill_rect          ; 下边
    mov bx, BOARD_Y-2
    mov cx, 0xA402          ; 左边: 宽 2 高 164
    call fill_rect
    mov ax, BOARD_X+80
    call fill_rect          ; 右边
    pop es

    mov si, s_score
    mov dh, 4
    mov dl, 2
    call print_str
    mov si, s_lines
    mov dh, 7
    mov dl, 2
    call print_str
    mov si, s_level
    mov dh, 10
    mov dl, 2
    call print_str
    mov si, s_best
    mov dh, 17
    mov dl, 2
    call print_str
    mov si, s_next
    mov dh, 3
    mov dl, 27
    call print_str
    mov si, s_hold
    mov dh, 10
    mov dl, 27
    call print_str
    mov si, s_h6
    mov dh, 19
    mov dl, 26
    call print_str
    mov si, s_h7
    mov dh, 20
    mov dl, 26
    call print_str
    mov si, s_h1
    mov dh, 14
    mov dl, 26
    call print_str
    mov si, s_h2
    mov dh, 15
    mov dl, 26
    call print_str
    mov si, s_h3
    mov dh, 16
    mov dl, 26
    call print_str
    mov si, s_h4
    mov dh, 17
    mov dl, 26
    call print_str
    mov si, s_h5
    mov dh, 18
    mov dl, 26
    call print_str
    ret

; 重画整个游戏画面
draw_all:
    push es
    mov ax, 0xA000
    mov es, ax
    call draw_board
    cmp byte [gameover], 1
    je  .skip
    call draw_ghost         ; 先画影子, 再画真方块盖在上面
    call draw_piece
.skip:
    call draw_next
    call draw_hold
    call draw_stats
    pop es
    ret

draw_board:
    mov si, board
    xor bh, bh              ; 行
.r: xor bl, bl              ; 列
.c: mov dl, [si]
    inc si
    call draw_cell
    inc bl
    cmp bl, BOARD_W
    jb  .c
    inc bh
    cmp bh, BOARD_H
    jb  .r
    ret

; 画正在下落的方块
draw_piece:
    mov ax, [cur_x]
    mov [t_x], ax
    mov ax, [cur_y]
    mov [t_y], ax
    mov al, [cur_rot]
    mov [t_rot], al
    mov al, [cur_piece]
    inc al
    cmp byte [grounded], 1
    jne .norm
    add al, 8               ; 已落地(锁定计时中): 用暗色版, 提示"快锁定了"
.norm:
    mov [pcolor], al
    jmp draw_shape

; 画影子: 从当前位置一直往下试, 找到最后一个不碰撞的 y
draw_ghost:
    mov ax, [cur_x]
    mov [t_x], ax
    mov ax, [cur_y]
    mov [t_y], ax
    mov al, [cur_rot]
    mov [t_rot], al
.down:
    inc word [t_y]
    call collide
    jnc .down
    dec word [t_y]          ; 退回最后一个合法位置
    mov byte [pcolor], 8    ; 颜色表第 8 项 = 暗灰
    jmp draw_shape

; 按 t_x, t_y, t_rot 位置、pcolor 颜色画出当前方块形状
draw_shape:
    mov bl, [cur_piece]
    mov bh, [t_rot]
    call get_mask
    mov si, ax
    xor cx, cx
.l: test si, 0x8000
    jz  .n
    mov al, cl
    shr al, 2
    xor ah, ah
    add ax, [t_y]
    mov bh, al              ; 行
    mov al, cl
    and al, 3
    xor ah, ah
    add ax, [t_x]
    mov bl, al              ; 列
    mov dl, [pcolor]
    call draw_cell
.n: shl si, 1
    inc cl
    cmp cl, 16
    jb  .l
    ret

; 右侧"下一个"预览. 利用 draw_cell 的相对坐标: 列 13.. 行 3.. 刚好落在棋盘右边
draw_next:
    mov ax, 216
    mov bx, 36
    mov cx, 0x2830          ; 宽 48 高 40
    xor dl, dl
    call fill_rect          ; 先擦掉旧的
    mov bl, [next_piece]
    xor bh, bh
    call get_mask
    mov si, ax
    xor cx, cx
.l: test si, 0x8000
    jz  .n
    mov bl, cl
    and bl, 3
    add bl, 13              ; 列
    mov bh, cl
    shr bh, 2
    add bh, 3               ; 行
    mov dl, [next_piece]
    inc dl
    call draw_cell
.n: shl si, 1
    inc cl
    cmp cl, 16
    jb  .l
    ret

; 右侧"暂存"区 (列 13.., 行 9..). 暂存区为空时只擦背景
draw_hold:
    mov ax, 216
    mov bx, 88
    mov cx, 0x1830          ; 宽 48 高 24
    xor dl, dl
    call fill_rect
    cmp byte [hold_piece], 0xFF
    je  .ret
    mov bl, [hold_piece]
    xor bh, bh
    call get_mask
    mov si, ax
    xor cx, cx
.l: test si, 0x8000
    jz  .n
    mov bl, cl
    and bl, 3
    add bl, 13
    mov bh, cl
    shr bh, 2
    add bh, 9
    mov dl, [hold_piece]
    inc dl
    call draw_cell
.n: shl si, 1
    inc cl
    cmp cl, 16
    jb  .l
.ret:
    ret

; 分数/行数/等级/状态
draw_stats:
    mov ax, [score]
    mov dx, [score+2]
    mov bx, 0x0502          ; BH=行 5, BL=列 2
    call print_num
    mov ax, [lines]
    xor dx, dx
    mov bx, 0x0802
    call print_num
    xor ax, ax
    mov al, [level]
    xor dx, dx
    mov bx, 0x0B02
    call print_num

    mov ax, [score]         ; 32 位比较: 先比高字, 高字相等再比低字
    mov dx, [score+2]
    cmp dx, [hiscore+2]
    jb  .nohi
    ja  .newhi
    cmp ax, [hiscore]
    jbe .nohi
.newhi:
    mov [hiscore], ax
    mov [hiscore+2], dx
.nohi:
    mov ax, [hiscore]
    mov dx, [hiscore+2]
    mov bx, 0x1202          ; 行 18, 列 2
    call print_num

    mov ax, 8               ; 清空状态区
    mov bx, 112
    mov cx, 0x1068
    xor dl, dl
    call fill_rect
    cmp byte [gameover], 1
    jne .p
    mov si, s_over
    mov dh, 14
    mov dl, 2
    call print_str
    mov si, s_restart
    mov dh, 15
    mov dl, 2
    call print_str
    ret
.p: cmp byte [paused], 1
    jne .ret
    mov si, s_pause
    mov dh, 14
    mov dl, 2
    call print_str
.ret:
    ret

; 打印以 0 结尾的字符串. SI=字符串, DH=行, DL=列 (文本网格 40x25, 每格 8x8 像素)
print_str:
    mov ah, 2               ; INT 10h/02h: 设置光标
    xor bh, bh
    int 0x10
.l: lodsb
    test al, al
    jz  .d
    mov ah, 0x0E            ; INT 10h/0Eh: 电传打字输出
    mov bx, 0x000F          ; BH=页0, BL=白色
    int 0x10
    jmp .l
.d: ret

; 蜂鸣超时检查. 输入 DX = 当前滴答 (保留). 响满 2 个滴答(约 0.1 秒)就关扬声器
beep_service:
    cmp byte [beep_on], 0
    je  .ret
    push ax
    mov ax, dx
    sub ax, [beep_start]
    cmp ax, 2
    jb  .done
    in  al, 0x61
    and al, 0xFC            ; 清除 bit0(定时器门控) 和 bit1(扬声器使能)
    out 0x61, al
    mov byte [beep_on], 0
.done:
    pop ax
.ret:
    ret

; 等待 CX 个滴答 (期间用 hlt 省 CPU, 并照常关蜂鸣)
wait_ticks:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, cx              ; BX = 要等的滴答数
    xor ah, ah
    int 0x1A
    mov si, dx              ; SI = 起始时刻
.w: hlt
    xor ah, ah
    int 0x1A
    call beep_service
    sub dx, si
    cmp dx, bx
    jb  .w
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; 清空键盘缓冲区 (动画期间堆积的按键作废)
flush_keys:
    push ax
.l: mov ah, 1
    int 0x16
    jz  .d
    xor ah, ah
    int 0x16
    jmp .l
.d: pop ax
    ret

; 消行闪烁动画. 先找出满行记入 fullrow[], 有满行才闪: 白 -> 原色 重复 3 次, 最后停在白色
flash_lines:
    push ax
    push bx
    push cx
    push si
    mov byte [flash_cnt], 0
    xor bx, bx              ; BX = 行号
.scan:
    imul si, bx, BOARD_W
    add si, board
    mov cx, BOARD_W
.chk:
    lodsb
    test al, al
    jz  .nf
    loop .chk
    mov byte [fullrow+bx], 1
    inc byte [flash_cnt]
    jmp .nx
.nf:
    mov byte [fullrow+bx], 0
.nx:
    inc bx
    cmp bx, BOARD_H
    jb  .scan
    cmp byte [flash_cnt], 0
    je  .ret
    mov ax, 1356            ; 880 Hz: 消行
    call beep
    call draw_all           ; 先把刚锁定的方块画出来
    mov byte [flash_n], 3
.again:
    mov byte [flash_mode], 1
    call paint_full_rows
    mov cx, 2
    call wait_ticks
    mov byte [flash_mode], 0
    call paint_full_rows
    mov cx, 2
    call wait_ticks
    dec byte [flash_n]
    jnz .again
    mov byte [flash_mode], 1
    call paint_full_rows    ; 最后停在白色, 紧接着行被删除
    mov cx, 3
    call wait_ticks
    call flush_keys
.ret:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; 把满行画成白色 (flash_mode=1) 或按棋盘原色重画 (flash_mode=0)
paint_full_rows:
    push es
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, 0xA000
    mov es, ax
    xor di, di              ; DI = 行号
.row:
    mov bx, di
    cmp byte [fullrow+bx], 0
    je  .next
    cmp byte [flash_mode], 0
    jne .white
    imul si, di, BOARD_W
    add si, board           ; SI -> 这一行的棋盘数据
    mov bx, di
    mov bh, bl              ; BH = 行
    xor bl, bl              ; BL = 列
.c: mov dl, [si]
    inc si
    call draw_cell
    inc bl
    cmp bl, BOARD_W
    jb  .c
    jmp .next
.white:
    mov ax, BOARD_X
    mov bx, di
    shl bx, 3
    add bx, BOARD_Y
    mov cx, 0x0850          ; 高 8 宽 80: 整行铺白
    mov dl, 15
    call fill_rect
.next:
    inc di
    cmp di, BOARD_H
    jb  .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret

; 标题画面: 七色方块 + 文字, 等任意键. 之后清屏
title_screen:
    push es
    mov ax, 0xA000
    mov es, ax
    mov ax, 80
    mov dl, 1
.d: mov bx, 60
    mov cx, 0x1010          ; 16x16 的大方块
    call fill_rect
    add ax, 24
    inc dl
    cmp dl, 8
    jb  .d
    mov si, s_title
    mov dh, 11
    mov dl, 14
    call print_str
    mov si, s_sub
    mov dh, 13
    mov dl, 9
    call print_str
    mov si, s_press
    mov dh, 18
    mov dl, 13
    call print_str
.wait:
    hlt
    mov ah, 1
    int 0x16
    jz  .wait
    xor ah, ah
    int 0x16                ; 取走这个按键
    xor di, di              ; 清屏: 64000 字节 = 32000 个字
    xor ax, ax
    mov cx, 32000
    rep stosw
    pop es
    ret

; 按当前等级设置 VGA 调色板 (DAC).
;   端口 0x3C8: 写入要修改的起始颜色下标
;   端口 0x3C9: 连续写 R, G, B (每个分量 0..63, 共 6 位), 下标自动递增
; 下标 1..8 = 7 种方块 + 影子, 取第 (等级-1) mod 5 套主题; 下标 9 = 边框, 每级一种颜色
apply_palette:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [level]
    dec al
    xor ah, ah
    mov bl, 5
    div bl                  ; AH = (等级-1) mod 5 = 主题号
    mov al, ah
    xor ah, ah
    shl ax, 3               ; 主题号 * 8
    mov si, ax
    shl ax, 1               ; 主题号 * 16
    add si, ax              ; SI = 主题号 * 24 (每套 8 种颜色 x 3 字节)
    add si, themes
    mov dx, 0x3C8
    mov al, 1
    out dx, al              ; 从颜色下标 1 开始
    inc dx                  ; DX = 0x3C9
    mov cx, 24
    rep outsb               ; DS:SI -> 端口 DX, 共 24 字节

    sub si, 24              ; 回到这套主题的开头
    dec dx
    mov al, 17
    out dx, al              ; 颜色下标 17..23 = 7 种方块的暗色版本
    inc dx
    mov cx, 21
.dim:
    lodsb
    shr al, 1               ; 每个 RGB 分量减半
    out dx, al
    loop .dim

    mov al, [level]
    dec al
    xor ah, ah
    mov si, ax
    shl ax, 1
    add si, ax              ; SI = (等级-1) * 3
    add si, border_tbl
    dec dx                  ; 回到 0x3C8
    mov al, 9
    out dx, al              ; 颜色下标 9
    inc dx
    mov cx, 3
    rep outsb
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; 打印无符号 32 位十进制数. DX:AX=数值, BH=行, BL=列. 要求 ES=0xA000 (先擦背景)
; 32 位除以 10 用两次 16 位 div: 先除高字, 余数作为下一步被除数的高字, 再除低字
print_num:
    pusha
    mov si, ax              ; SI = 低字
    mov di, dx              ; DI = 高字
    push bx                 ; 保存 行/列
    mov al, bl
    xor ah, ah
    shl ax, 3               ; x = 列*8
    mov bl, bh
    xor bh, bh
    shl bx, 3               ; y = 行*8
    mov cx, 0x0850          ; 高 8 宽 80 (最多 10 位数字)
    xor dl, dl
    call fill_rect
    pop dx                  ; DH=行, DL=列
    mov ah, 2
    xor bh, bh
    int 0x10                ; 设置光标
    xor cx, cx              ; CX = 数字个数
.div:
    mov ax, di
    xor dx, dx
    mov bx, 10
    div bx                  ; AX = 高字/10, DX = 余数
    mov di, ax
    mov ax, si              ; DX:AX = 余数:低字
    div bx                  ; AX = 商的低字, DX = 最终余数 (本位数字)
    mov si, ax
    push dx
    inc cx
    mov ax, di
    or  ax, si
    jnz .div                ; 商不为 0 就继续
.pr:
    pop ax
    add al, '0'
    mov ah, 0x0E
    mov bx, 0x000F
    push cx
    int 0x10
    pop cx
    loop .pr
    popa
    ret

; ============================================================
;  数据
; ============================================================
; 7 种方块 x 4 个旋转状态, 顺序: I O T S Z J L
; 每项 16 位 = 4 行 x 4 列, 每行一个半字节(nibble), 1 = 有方块
pieces:
    ; I
    dw 0b0000_1111_0000_0000, 0b0010_0010_0010_0010, 0b0000_0000_1111_0000, 0b0100_0100_0100_0100
    ; O
    dw 0b0110_0110_0000_0000, 0b0110_0110_0000_0000, 0b0110_0110_0000_0000, 0b0110_0110_0000_0000
    ; T
    dw 0b0100_1110_0000_0000, 0b0100_0110_0100_0000, 0b0000_1110_0100_0000, 0b0100_1100_0100_0000
    ; S
    dw 0b0110_1100_0000_0000, 0b0100_0110_0010_0000, 0b0000_0110_1100_0000, 0b1000_1100_0100_0000
    ; Z
    dw 0b1100_0110_0000_0000, 0b0010_0110_0100_0000, 0b0000_1100_0110_0000, 0b0100_1100_1000_0000
    ; J
    dw 0b1000_1110_0000_0000, 0b0110_0100_0100_0000, 0b0000_1110_0010_0000, 0b0100_0100_1100_0000
    ; L
    dw 0b0010_1110_0000_0000, 0b0100_0100_0110_0000, 0b0000_1110_1000_0000, 0b1100_0100_0100_0000

; 棋盘值(0=空, 1..7=方块号+1) -> VGA 默认调色板颜色
colors    db 0, 1, 2, 3, 4, 5, 6, 7, 8   ; 棋盘值 -> 调色板下标 (RGB 由 apply_palette 按等级写入; 8 = 影子)
          db 17, 18, 19, 20, 21, 22, 23  ; 值 9..15 = 方块 1..7 的暗色版 (落地锁定中使用)
kicks     db 0, 1, -1, 2, -2

; 5 套方块主题, 每套 8 种颜色 (I O T S Z J L 影子), 每种 R,G,B (0..63)
themes:
    ; 0: 经典鲜艳
    db  0,56,56,  63,56, 0,  50,12,50,  12,56,12,  60,12,12,  16,24,63,  63,38, 6,  14,14,14
    ; 1: 粉彩
    db 35,58,63,  63,60,38,  55,38,60,  40,60,40,  63,40,40,  42,48,63,  63,50,36,  18,18,22
    ; 2: 霓虹
    db  0,63,45,  63,63, 0,  63, 0,48,  30,63, 0,  63, 8,20,  24,12,63,  63,32, 0,  14,10,18
    ; 3: 冷色
    db 20,50,63,  30,58,63,  40,40,63,  10,45,55,  30,30,60,  10,20,50,  45,55,63,  10,14,20
    ; 4: 火焰
    db 63,50,10,  63,63,30,  63,25, 5,  55,40, 0,  63, 8, 0,  45,10, 5,  63,30,20,  20,10,10

; 10 个等级的边框颜色 (R,G,B)
border_tbl:
    db 32,32,32,  20,40,63,   0,55,45,  30,60,20,  63,60, 0
    db 63,40, 0,  63,20,10,  63,10,30,  45,10,63,  63,63,63
score_tbl dw 0, 100, 300, 500, 800

s_score   db 'SCORE', 0
s_lines   db 'LINES', 0
s_level   db 'LEVEL', 0
s_next    db 'NEXT', 0
s_best    db 'BEST', 0
s_title   db 'T E T R I S', 0
s_sub     db 'x86 REAL MODE ASSEMBLY', 0
s_press   db 'PRESS ANY KEY', 0
s_hold    db 'HOLD', 0
s_over    db 'GAME OVER', 0
s_restart db 'R: RESTART', 0
s_pause   db 'PAUSED', 0
s_h1      db 'ARROWS/WASD', 0
s_h2      db 'UP: ROTATE', 0
s_h3      db 'SPACE: DROP', 0
s_h4      db 'P: PAUSE', 0
s_h5      db 'ESC: RESET', 0
s_h6      db 'C: HOLD', 0
s_h7      db 'M: SOUND', 0

; 变量 (放在已加载的内存里, 可读可写)
cur_piece  db 0
cur_rot    db 0
cur_x      dw 0             ; 方块 4x4 框左上角在棋盘中的位置 (可为负)
cur_y      dw 0
t_x        dw 0             ; 试探用的临时位置 (碰撞检测用)
t_y        dw 0
t_rot      db 0
next_piece db 0
score      dd 0             ; 32 位, 低字在 [score], 高字在 [score+2] (小端)
lines      dw 0
level      db 1
paused     db 0
gameover   db 0
dirty      db 1             ; 1 = 需要重画
last_tick  dw 0
seed       dw 0x1234
pcolor     db 0             ; draw_shape 用的颜色
hiscore    dd 0
fullrow    times BOARD_H db 0   ; 每行是否满行 (消行动画用)
flash_cnt  db 0
flash_n    db 0
flash_mode db 0                 ; 0 = 原色, 1 = 白色
grounded   db 0             ; 1 = 方块已落地, 正在锁定计时
reset_req  db 0             ; 本轮有成功的移动/旋转
lock_resets db 0            ; 本块已重置计时的次数
lock_start dw 0             ; 开始(或上次重置)锁定计时的滴答
hold_piece db 0xFF          ; 暂存的方块号, 0xFF = 空
hold_used  db 0             ; 本块是否已暂存过
mute       db 0
beep_on    db 0
beep_start dw 0
bag_pos    db 7             ; 初值 7 = 袋子是空的, 第一次取牌时会先装袋
bag        times 7 db 0
board      times BOARD_W*BOARD_H db 0

; 补齐到 (SECTORS+1) 个扇区, 同时若代码超出会在汇编时报错
times (SECTORS+1)*512-($-$$) db 0
