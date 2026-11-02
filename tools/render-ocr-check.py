#!/usr/bin/env python3
# 渲染级乱码检测: screendump → 模板匹配 OCR (font16.h 字形) → 与文本缓冲区对比
import socket, subprocess, time, os, re, threading, sys

NEWOS = "/home/mfw/Documents/claude001/NewOS"
IMG = f"{NEWOS}/build/os.img"
MON = "/tmp/newos-repro/mon3.sock"
SHOT = "/tmp/newos-repro/screen.ppm"
try: os.unlink(MON)
except FileNotFoundError: pass

# ---- 解析 font16.h → 256 字形模板 ----
src = open(f"{NEWOS}/kernel/font16.h").read()
tables = re.findall(r"\{([^}]*)\}", src)[:256]
fonts = []
for t in tables:
    vals = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", t)]
    assert len(vals) == 16, f"bad glyph: {len(vals)}"
    fonts.append(vals)

def glyph_bits(rows):
    bits = []
    for r in rows:
        bits.extend([(r >> b) & 1 for b in range(7, -1, -1)])
    return bits

TEMPLATES = [glyph_bits(f) for f in fonts]

# ---- 启动 QEMU, 串口读线程 ----
serial_buf = bytearray()
def serial_reader(pipe):
    fd = pipe.fileno()
    while True:
        d = os.read(fd, 65536)
        if not d: break
        serial_buf.extend(d)

qemu = subprocess.Popen(
    ["qemu-system-i386", "-fda", IMG, "-snapshot", "-display", "none",
     "-monitor", f"unix:{MON},server,nowait", "-serial", "stdio"],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
threading.Thread(target=serial_reader, args=(qemu.stdout,), daemon=True).start()

for _ in range(100):
    if os.path.exists(MON): break
    time.sleep(0.05)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MON); sock.settimeout(10)

def send(cmd, wait=0.15):
    sock.sendall((cmd + "\n").encode())
    out = b""
    end = time.time() + 3
    while time.time() < end:
        try:
            d = sock.recv(65536)
            if not d: break
            out += d
            if out.rstrip().endswith(b"(qemu)"): break
        except socket.timeout: break
    time.sleep(wait)
    return out.decode(errors="replace")

def wait_serial(pat, timeout=20):
    end = time.time() + timeout
    while time.time() < end:
        if re.search(pat.encode(), bytes(serial_buf)): return True
        time.sleep(0.1)
    return False

# ---- 等 splash 结束 + shell 提示符出现 ----
if not wait_serial(r"\[SP\] (done|skipped)"): print("TIMEOUT: no [SP]"); sys.exit(1)
time.sleep(1.0)
send("screendump " + SHOT)
time.sleep(0.5)
drain = send("")
print("[FONT] boot line:", re.findall(rb"\[FONT\][^\r\n]*", bytes(serial_buf)))

# ---- 读文本缓冲区 ----
def read_b8000():
    r = send("xp /4000wb 0xb8000", wait=0.05)
    out = []
    for line in r.splitlines():
        if ':' not in line or '\x1b' in line: continue
        for tok in line.split(':', 1)[1].split():
            if tok.startswith('0x'): out.append(int(tok, 16))
    return out

chars = read_b8000()
if len(chars) < 4000:   # 回复被截断 → 分块重读
    chars = []
    for i in range(4):
        r = send(f"xp /1000wb {0xb8000 + i*1000:#x}", wait=0.05)
        for line in r.splitlines():
            if ':' not in line or '\x1b' in line: continue
            for tok in line.split(':', 1)[1].split():
                if tok.startswith('0x'): chars.append(int(tok, 16))
print("buffer bytes read:", len(chars))
buf = [chr(chars[y*160 + x*2]) if 32 <= chars[y*160 + x*2] < 127 else ' '
       for y in range(25) for x in range(80)]

# ---- 解析 PPM 并 OCR ----
try:
    from PIL import Image
    im = Image.open(SHOT).convert("L")
    W, H = im.size
    px = im.load()
except ImportError:
    # 手写 PPM 解析
    data = open(SHOT, "rb").read()
    toks = []
    i = 0
    while len(toks) < 2:
        i = data.find(b"\n", i) + 1
        if data[i:i+1] != b"#": toks.append(int(data[i:data.find(b"\n", i)]))
    W, H = toks
    data = data[data.find(b"\n", data.find(b"\n", data.find(b"\n")+1)+1)+1:]
    px_vals = [int(b) for b in data]
    def getpx(x, y):
        off = (y*W + x) * 3
        return (px_vals[off] + px_vals[off+1] + px_vals[off+2]) // 3
    def px_get(x, y): return getpx(x, y)

def cell_bits(x, y):
    # VGA 文本模式: 9x16 单元, 字形占前 8 列 (第 9 列 = 背景/dup9)
    bits = []
    for py in range(16):
        for dx in range(8):
            v = px[x*9 + dx, y*16 + py]
            bits.append(1 if v > 128 else 0)
    return bits

def best_match(bits):
    best, bestd = ord('?'), 999
    for c in range(256):
        d = sum(a != b for a, b in zip(bits, TEMPLATES[c]))
        if d < bestd:
            bestd, best = d, c
    return best, bestd

print("screen WxH:", W, H)
mismatch = 0
for y in range(25):
    row_ren = ""
    row_buf = buf[y*80:(y+1)*80]
    for x in range(80):
        b = buf[y*80 + x]
        if b == ' ': row_ren += ' '; continue
        bits = cell_bits(x, y)
        c, d = best_match(bits)
        if c != ord(b):
            mismatch += 1
            row_ren += f"[{b}→{chr(c) if 32 <= c < 127 else hex(c)}]"
        else:
            row_ren += b
    print(f"{y:2d} REN: {row_ren.rstrip()}")
print("TOTAL MISMATCH:", mismatch)

send("quit"); qemu.wait(timeout=10)
