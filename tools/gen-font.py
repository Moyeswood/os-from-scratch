#!/usr/bin/env python3
"""PSF 控制台字体 → NewOS C 头文件转换器

用法:
  gen-font.py <font.psf.gz> <输出头文件名> --name <数组名>

提取前 256 个字形, 输出 VGA 字符发生器格式的 C 数组:
每字形 height 字节, 每字节一行, MSB = 最左像素 (与 font8x8.h 一致)。

支持两种格式 (Arch terminus-font 包里 PSF1/PSF2 混用):
  PSF1: u16 magic=0x0436, u8 mode, u8 charsize, 然后 256*charsize 字节。
        宽度隐含为 8, 无 Unicode 表, 字形 i = 码位 i。
  PSF2: u32 magic=0x864AB572, u32 version, u32 headersize, u32 flags(bit0=有
        Unicode 表), u32 length, u32 charsize, u32 height, u32 width,
        然后 length*charsize 字节字形, 然后 Unicode 表。
"""
import gzip, struct, sys, os

def load_psf(path):
    raw = gzip.open(path, 'rb').read() if path.endswith('.gz') else open(path, 'rb').read()
    if raw[:2] == b'\x36\x04':                      # PSF1
        mode, charsize = raw[2], raw[3]
        nglyphs = 512 if mode & 1 else 256
        height = charsize                              # 宽度 8 → 每行 1 字节
        glyphs = raw[4 : 4 + nglyphs * charsize]
        utable = None
        if mode & 2:                                   # bit1 = 有 Unicode 表
            pos = 4 + nglyphs * charsize
            table, idx = {}, 0
            while pos < len(raw) - 1:
                cp = raw[pos] | (raw[pos + 1] << 8)
                pos += 2
                if cp == 0xFFFF:
                    idx += 1
                else:
                    table.setdefault(idx, []).append(cp)
            utable = table
        return 8, height, nglyphs, charsize, glyphs, utable
    magic, ver, hdrsize, flags, length, charsize, height, width = \
        struct.unpack_from('<8I', raw, 0)
    assert magic == 0x864AB572, f"不是 PSF: {path}"
    glyphs = raw[hdrsize : hdrsize + length * charsize]
    utable = None
    if flags & 1:
        pos = hdrsize + length * charsize
        table, idx = {}, 0
        while pos < len(raw) - 1:
            cp, = struct.unpack_from('<H', raw, pos)
            pos += 2
            if cp == 0xFFFF:
                idx += 1
            else:
                table.setdefault(idx, []).append(cp)
        utable = table
    return width, height, length, charsize, glyphs, utable

def main():
    src, out_h = sys.argv[1], sys.argv[2]
    name = sys.argv[sys.argv.index('--name') + 1] if '--name' in sys.argv else 'font_data'
    width, height, length, charsize, glyphs, utable = load_psf(src)

    char_to_glyph = {i: i for i in range(min(length, 256))}
    if utable:
        for gi, cps in utable.items():
            for cp in cps:
                if cp < 256:
                    char_to_glyph[cp] = gi

    nbytes_row = (width + 7) // 8
    lines = [
        f"// 由 {os.path.basename(src)} 生成: {width}x{height} 点阵字体 (Terminus)",
        f"// 256 个字形 (码位 0..255), 每字形 {height} 字节, 每字节一行, MSB=最左像素",
        f"// 生成命令: python3 tools/gen-font.py <源psf> {os.path.basename(out_h)} --name {name}",
        f"#ifndef {name.upper()}_H",
        f"#define {name.upper()}_H",
        "",
        '#include "types.h"',
        "",
        f"static const uint8_t {name}[256][{height}] = {{",
    ]
    for c in range(256):
        gi = char_to_glyph.get(c, 0)
        g = glyphs[gi * charsize : (gi + 1) * charsize]
        rows = []
        for r in range(height):
            v = 0
            for b in range(nbytes_row):
                v = (v << 8) | g[r * nbytes_row + b]
            rows.append(v >> (nbytes_row * 8 - width))
        lines.append('    {%s},  /* 0x%02X */' % (', '.join('0x%02X' % x for x in rows), c))
    lines += ["};", "", "#endif", ""]
    with open(out_h, 'w') as f:
        f.write('\n'.join(lines))
    print(f"OK: {os.path.basename(src)} {width}x{height} ({length} glyphs) -> {out_h}")

if __name__ == '__main__':
    main()
