// ============================================================
//  VGA 字符发生器字体管理 (纯硬件编程, 不依赖 BIOS)
//
//  字体数据: font12.h / font14.h / font16.h (由 Terminus PSF 生成,
//  见 tools/gen-font.py)。每套 256 个字形 (码位 0..255),
//  每字形 height 字节, 每字节一行, MSB = 最左像素 —
//  与 VGA 字符发生器 RAM (平面 2) 的存储格式完全一致。
//
//  为什么不用 BIOS INT 10h AX=1100h: SeaVGABIOS 没实现它
//  (实测平面 2 内容不变、CRTC 不重配), 而且直接写寄存器
//  才是真正的 VGA 硬件编程。
//
//  ★文本模式的平面寻址陷阱 (QEMU vga.c 实测语义):
//  - 写: SR4 bit2 (SEQ_MODE)=0 时写入被 odd/even 奇偶限制,
//        且 SR4bit2=0 + CR17 字模式 → 地址 >>= 1
//    → 写平面 2 前必须置 SR4 bit2 = 1
//  - 读: GR5 bit4 (HOST_ODD_EVEN)=1 时平面 = 读映射&2 | 地址bit0
//    → 读平面 2 前必须清 GR5 bit4
//  - GR6 bit1 (CHAIN_ODD_EVEN)=1 时地址 bit0 被屏蔽 (奇地址读偶字节)
//    → 读写前都必须清 GR6 bit1
//  - GR6 bits2-3 = 内存映射, 文本模式默认 3 (B8000),
//    访问平面必须改成 1 (A0000 64K 窗口)
//
//  CRTC 行高/行数编程 (字号切换):
//    CR09 = h-1           每字符扫描线数 (行高)
//    CR12 = rows*h-1      垂直显示结束 (配合 CR07 bit1/bit6 的高位)
//    CR0A/0B = h-2/h-1    光标形状
//    CR14 = h-1           下划线位置
//  帧的其他时序 (垂直总扫描线 449 等) 不动 — 换行高只是让
//  400 线活动区里装下 25/28/33 行字符, 帧同步不受影响。
// ============================================================

#include "font.h"
#include "font12.h"
#include "font14.h"
#include "font16.h"
#include "vga.h"
#include "serial.h"
#include "io.h"

static uint8_t current_height = 16;

uint8_t font_current(void) {
    return current_height;
}

static const uint8_t* font_table(uint8_t h) {
    if (h == 12) return &font12[0][0];
    if (h == 14) return &font14[0][0];
    return &font16[0][0];
}

static void crtc_write(uint8_t reg, uint8_t val) {
    outb(0x3D4, reg);
    outb(0x3D5, val);
}

static uint8_t crtc_read(uint8_t reg) {
    outb(0x3D4, reg);
    return inb(0x3D5);
}

// 把字体表写进字符发生器 RAM (平面 2)。期间关中断防止写屏干扰。
// ★VGA 索引寄存器陷阱: 0x3C4/0x3CE 是粘性索引 — 每次写数据口
// (0x3C5/0x3CF) 前都必须重新选中目标寄存器索引!
static void font_write_plane2(const uint8_t* tbl, uint8_t h) {
    __asm__ volatile("cli");

    // 保存现场
    outb(0x3C4, 2);  uint8_t sr2 = inb(0x3C5);
    outb(0x3C4, 4);  uint8_t sr4 = inb(0x3C5);
    outb(0x3CE, 5);  uint8_t gr5 = inb(0x3CF);
    outb(0x3CE, 6);  uint8_t gr6 = inb(0x3CF);

    // SR4: bit2=1 关 odd/even 写入奇偶限制, bit3=0 关 chain4
    outb(0x3C4, 4); outb(0x3C5, (sr4 | 0x04) & ~0x08);
    // GR5: bit4=0 关 odd/even 地址交织, 写模式 0 (保持文本模式默认)。
    // 为什么写模式 0 能直写数据: 写模式 0 的 CPU 字节经 GR3 旋转后,
    // 仅在 GR1 (Enable Set/Reset) 置位的平面被 GR0 (Set/Reset) 替换 —
    // 文本模式 GR1=0 → set/reset 完全禁用 → CPU 字节原样进入平面。
    // (写模式 1 写的是锁存器不是 CPU 字节, 不能用!)
    outb(0x3CE, 5); outb(0x3CF, gr5 & ~0x10);
    // GR6: bit1=0 关 chain odd/even (地址 bit0 屏蔽), 映射 A0000 64K
    outb(0x3CE, 6); outb(0x3CF, (gr6 & ~(0x0C | 0x02)) | 0x04);
    // SR2: Map Mask = 0x04 (只写平面 2)
    outb(0x3C4, 2); outb(0x3C5, 0x04);

    // 写字体: 256 字形 × h 字节, 逐字节写到 A0000 窗口 (平面 2 偏移 0)
    volatile uint8_t* plane2 = (volatile uint8_t*)0xA0000;
    for (int i = 0; i < 256 * h; i++) {
        plane2[i] = tbl[i];
    }

    // 恢复现场
    outb(0x3C4, 4); outb(0x3C5, sr4);
    outb(0x3CE, 6); outb(0x3CF, gr6);
    outb(0x3CE, 5); outb(0x3CF, gr5);
    outb(0x3C4, 2); outb(0x3C5, sr2);
    __asm__ volatile("sti");
}

// CRTC 重配: 行高 h → 400/h 行文本 (见文件头注释)
static void font_set_crtc(uint8_t h) {
    int rows = 400 / h;
    uint16_t vde = rows * h - 1;        // 垂直显示结束 (10 位)

    crtc_write(0x09, h - 1);            // 最大扫描线 = 行高 - 1
    uint8_t cr07 = crtc_read(0x07);     // 溢出寄存器: VDE 的高 2 位
    cr07 = (cr07 & ~0x42)
         | (((vde >> 8) & 1) << 1)      // bit1 = VDE bit8
         | (((vde >> 9) & 1) << 6);     // bit6 = VDE bit9
    crtc_write(0x07, cr07);
    crtc_write(0x12, vde & 0xFF);
    crtc_write(0x0A, h - 2);            // 光标起始扫描线
    crtc_write(0x0B, h - 1);            // 光标结束扫描线
    crtc_write(0x14, h - 1);            // 下划线扫描线
}

// 从平面 2 读回字体与源数据比对, 返回不匹配字节数 (0 = 一致)。
static int font_selftest(const uint8_t* tbl, uint8_t h) {
    __asm__ volatile("cli");

    // 保存现场
    outb(0x3CE, 4);  uint8_t gr4 = inb(0x3CF);
    outb(0x3CE, 5);  uint8_t gr5 = inb(0x3CF);
    outb(0x3CE, 6);  uint8_t gr6 = inb(0x3CF);

    // 每次写数据口前重新选索引 (同 font_write_plane2 的陷阱注释)!
    // GR4: Read Map = 2 (读平面 2)
    outb(0x3CE, 4); outb(0x3CF, 2);
    // GR5: bit4=0 关 odd/even (平面 = 读映射, 不再被地址奇偶劫持)
    outb(0x3CE, 5); outb(0x3CF, gr5 & ~0x10);
    // GR6: bit1=0 关 chain odd/even (奇地址屏蔽), 映射 A0000 64K
    outb(0x3CE, 6); outb(0x3CF, (gr6 & ~(0x0C | 0x02)) | 0x04);

    volatile uint8_t* plane2 = (volatile uint8_t*)0xA0000;
    int n = 256 * h;
    int bad = 0;
    int first_bad = -1;
    for (int i = 0; i < n; i++) {
        if (plane2[i] != tbl[i]) {
            if (first_bad < 0) first_bad = i;
            bad++;
        }
    }
    if (bad > 0) {
        // 诊断: 第一个不匹配字节 + 平面 2 开头 8 字节 vs 期望
        serial_puts(" first_bad="); serial_put_hex(first_bad);
        serial_puts(" exp=");        serial_put_hex(tbl[first_bad]);
        serial_puts(" got=");        serial_put_hex(plane2[first_bad]);
        for (int i = 0; i < 4; i++) {
            serial_puts(i == 0 ? " p2[0..3]=" : ",");
            serial_put_hex(plane2[i]);
        }
        for (int i = 0; i < 4; i++) {
            serial_puts(i == 0 ? " tb[0..3]=" : ",");
            serial_put_hex(tbl[i]);
        }
    }

    // 恢复现场
    outb(0x3CE, 6); outb(0x3CF, gr6);
    outb(0x3CE, 5); outb(0x3CF, gr5);
    outb(0x3CE, 4); outb(0x3CF, gr4);
    __asm__ volatile("sti");
    return bad;
}

void font_apply(uint8_t h) {
    if (h != 12 && h != 14 && h != 16) return;
    const uint8_t* tbl = font_table(h);

    // 1. 写字符发生器平面 2
    font_write_plane2(tbl, h);
    // 2. CRTC 行高/行数/光标
    font_set_crtc(h);
    // 3. 内核侧行数 + 清屏
    current_height = h;
    vga_set_rows(400 / h);
    vga_clear();
    // 4. 自测: 平面 2 读回比对 + CRTC 验证
    int bad = font_selftest(tbl, h);
    uint8_t msl = crtc_read(0x09) & 0x1F;
    uint8_t vde_lo = crtc_read(0x12);
    uint8_t vde_hi = (crtc_read(0x07) >> 1) & 1;
    serial_puts("[FONT] height="); serial_put_hex(h);
    serial_puts(" mismatch=");     serial_put_hex(bad);
    serial_puts(" scanline=");     serial_put_hex(msl);
    serial_puts(" vde=");          serial_put_hex((vde_hi << 8) | vde_lo);
    if (bad == 0 && msl == h - 1) {
        serial_puts(" OK\n\r");
    } else {
        serial_puts(" FAIL\n\r");
    }
}
