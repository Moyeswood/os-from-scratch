#!/usr/bin/env python3
# 验证 font 12/14/16 切换后各字号的真实渲染 (模板匹配 OCR)
import socket, subprocess, time, os, re, threading, sys

NEWOS = "/home/mfw/Documents/claude001/NewOS"
IMG = f"{NEWOS}/build/os-kernel.img"
MON = "/tmp/newos-repro/mon4.sock"
try: os.unlink(MON)
except FileNotFoundError: pass

def load_font(h):
    src = open(f"{NEWOS}/kernel/font{h}.h").read()
    tables = re.findall(r"\{([^}]*)\}", src)[:256]
    fonts = [[int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", t)] for t in tables]
    def bits(rows):
        b = []
        for r in rows:
            b.extend([(r >> i) & 1 for i in range(7, -1, -1)])
        return b
    return [bits(f) for f in fonts]

TEMPLATES = {12: load_font(12), 14: load_font(14), 16: load_font(16)}

serial_buf = bytearray()
def serial_reader(pipe):
    fd = pipe.fileno()
    while True:
        d = os.read(fd, 65536)
        if not d: break
        serial_buf.extend(d)

qemu = subprocess.Popen(
    ["qemu-system-i386", "-fda", IMG, "-snapshot", "-display", "vnc=127.0.0.1:9",
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

def type_text(s):
    for ch in s:
        if ch == ' ': send("sendkey spc", wait=0.04)
        else: send(f"sendkey {ch}", wait=0.04)

def read_b8000(rows):
    chars = []
    chunks = (rows * 160 + 999) // 1000
    for i in range(chunks):
        r = send(f"xp /1000wb {0xb8000 + i*1000:#x}", wait=0.03)
        for line in r.splitlines():
            if ':' not in line or '\x1b' in line: continue
            for tok in line.split(':', 1)[1].split():
                if tok.startswith('0x'): chars.append(int(tok, 16))
    return chars

def ocr(h, rows, shot):
    from PIL import Image
    im = Image.open(shot).convert("L")
    px = im.load()
    T = TEMPLATES[h]
    chars = read_b8000(rows)
    mis = 0
    for y in range(rows):
        for x in range(80):
            b = chars[y*160 + x*2]
            if b == 0x20: continue
            cell = [1 if px[x*9 + dx, y*h + py] > 128 else 0
                    for py in range(h) for dx in range(8)]
            best, bd = -1, 999
            for c in range(256):
                d = sum(a != tb for a, tb in zip(cell, T[c]))
                if d < bd: bd, best = d, c
            if best != b:
                mis += 1
                print(f"  MISMATCH h={h} y={y} x={x}: buf={chr(b)!r}(0x{b:02x}) rendered=0x{best:02x} dist={bd}")
    print(f"h={h}: {rows} rows checked, mismatches={mis}")
    return mis

if not wait_serial(r"\[SP\] (done|skipped)"): print("TIMEOUT [SP]"); sys.exit(1)
time.sleep(0.8)

total = 0
# 16 → 12 → 14 → 16
for h, rows in ((16, 25), (12, 33), (14, 28), (16, 25)):
    if h == 16 and rows == 25: pass  # 初始已是 16
    else:
        type_text(f"font {h}")
        send("sendkey ret", wait=1.0)
    type_text(f"hello {h}px")
    send("sendkey ret", wait=0.8)
    shot = f"/tmp/newos-repro/h{h}.ppm"
    send("screendump " + shot)
    total += ocr(h, rows, shot)

print("TOTAL MISMATCHES:", total)
send("quit");
try: qemu.wait(timeout=10)
except: qemu.kill()
