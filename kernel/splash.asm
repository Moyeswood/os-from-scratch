; ============================================================
;  开机画面字模绘制 (32 位保护模式版)
;  由用户的 helloworld/src/hello.asm 逐字移植:
;  原 16 位实模式 ES:DI 显存寻址 → 32 位绝对地址 (0xA0000 + 偏移),
;  原 pusha/popa → pushad/popad, 8 位循环计数 → 同构。
;
;  对外接口:
;    void splash_glyph(int x, int y, char c, int color, int scale);
;  在模式 13h (320x200x256) 的 (x, y) 处, 用 color 色以
;  scale 倍放大绘制字符 c 的 8x8 字模 (8*scale x 8*scale 像素)。
;
;  ★字模查找是线性扫描: 1 字节 ASCII 码 + 8 字节点阵,
;  找不到该字符就不画 (原版同款行为)。
; ============================================================

[BITS 32]
section .text
global splash_glyph

; 绘制状态 (供子程序共享, 教学可读性优先于寄存器压榨)
section .bss
splash_x:     resd 1
splash_y:     resd 1
splash_scale: resd 1
splash_color: resb 1

section .text

; ---------- 在 (splash_x, splash_y) 画 scale x scale 的色块 ----------
splash_block:
    pushad
    mov esi, 0xA0000
    ; 偏移 = y * 320 + x
    mov eax, [splash_y]
    mov edx, 320
    mul edx                 ; EAX = y * 320 (320x200 不会溢出)
    add eax, [splash_x]
    add esi, eax            ; ESI = 块左上角显存地址

    mov ecx, [splash_scale] ; ECX = 纵向行数
.block_y:
    push ecx
    mov edi, esi            ; EDI = 当前行起点
    mov ecx, [splash_scale] ; ECX = 横向像素数
    mov al, [splash_color]
.block_x:
    mov [edi], al           ; 写入颜色 (模式 13h: 1 字节 = 1 像素)
    inc edi
    dec ecx
    jnz .block_x
    add esi, 320            ; 到下一扫描行
    pop ecx
    dec ecx
    jnz .block_y
    popad
    ret

; ---------- 主绘制函数 ----------
splash_glyph:
    pushad
    mov ebp, esp
    ; 参数位置: pushad 压了 8 个寄存器 (32 字节), 返回地址 4 字节
    ; [ebp+36]=x, [ebp+40]=y, [ebp+44]=c, [ebp+48]=color, [ebp+52]=scale

    mov eax, [ebp+36]
    mov [splash_x], eax
    mov eax, [ebp+40]
    mov [splash_y], eax
    mov eax, [ebp+48]
    mov [splash_color], al
    mov eax, [ebp+52]
    mov [splash_scale], eax

    ; 1. 在 font_map 中查找字符的字模地址 (线性扫描)
    mov eax, [ebp+44]       ; AL = 字符
    mov esi, font_map
.find_char:
    mov bl, [esi]
    test bl, bl
    jz .draw_end            ; 表尾 0, 找不到就不画
    cmp bl, al
    je .found_char
    add esi, 9              ; 跳过 1 字节 ASCII + 8 字节点阵
    jmp .find_char

.found_char:
    inc esi                 ; ESI 指向 8 字节字模数据

    ; 2. 逐行逐位扫描字模, 置位处画 scale x scale 色块
    mov ecx, 8              ; 8 行
.draw_row:
    mov dl, [esi]           ; DL = 当前行字模字节
    inc esi
    mov bl, 8               ; 8 列
.draw_bit:
    shl dl, 1               ; 最高位移入 CF
    jnc .skip_pixel
    call splash_block       ; 在 (splash_x, splash_y) 画块
.skip_pixel:
    mov eax, [splash_scale]
    add [splash_x], eax     ; X += scale
    dec bl
    jnz .draw_bit

    ; 下一行字模: X 复位, Y += scale
    mov eax, [ebp+36]
    mov [splash_x], eax
    mov eax, [splash_scale]
    add [splash_y], eax
    dec ecx
    jnz .draw_row

.draw_end:
    popad
    ret

; ============================================================
;  字模数据 (原版 9 个 + 新增 M y Q w e n -)
; ============================================================
section .rodata
font_map:
    db 'H', 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00
    db 'E', 0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x7E, 0x00
    db 'L', 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00
    db 'O', 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00
    db 'W', 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00
    db 'R', 0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00
    db 'D', 0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00
    db ',', 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30
    db '!', 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00
    ; ---- 新增 (副标题用, 与原版同风格) ----
    db 'M', 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00
    db 'y', 0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C
    db 'Q', 0x3C, 0x66, 0x66, 0x66, 0x6E, 0x3C, 0x0C, 0x07
    db 'w', 0x00, 0x00, 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x36
    db 'e', 0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x66, 0x3C
    db 'n', 0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66
    db '-', 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00
    db 0
