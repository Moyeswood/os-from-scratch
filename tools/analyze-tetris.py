#!/usr/bin/env python3
# 修正版过滤: 方块(1..7) + 影子(8) + 落地暗色(17..23), 排除边框(9)和文字(15)
import socket, sys, time

sock_path = sys.argv[1]
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(sock_path); sock.settimeout(8)

def drain(wait=0.3):
    time.sleep(wait)
    try:
        while sock.recv(65536): pass
    except socket.timeout: pass

def send(cmd, wait=0.1):
    sock.sendall((cmd + "\n").encode())
    out = b""
    while True:
        try:
            d = sock.recv(1048576)
            if not d: break
            out += d
            if out.rstrip().endswith(b"(qemu)"): break
        except socket.timeout: break
    time.sleep(wait)
    return out.decode(errors='replace')

def type_text(s, wait=0.1):
    for ch in s:
        if ch == ' ': send("sendkey spc", wait=wait)
        elif ch == '\n': send("sendkey ret", wait=wait)
        else: send(f"sendkey {ch}", wait=wait)

def read_mem(addr, nbytes):
    data = bytearray()
    for off in range(0, nbytes, 3200):
        chunk = min(3200, nbytes - off)
        words = chunk // 4 + (1 if chunk % 4 else 0)
        r = send(f"xp /{words}wx {addr+off:#x}", wait=0.01)
        vals = []
        for line in r.splitlines():
            if ':' not in line or '\x1b' in line: continue
            for tok in line.split(':', 1)[1].split():
                if tok.startswith('0x'): vals.append(int(tok, 16))
        for v in vals:
            data += v.to_bytes(4, 'little')
    return bytes(data[:nbytes])

def read_rows(y0, h, x0=0, w=320):
    rows = []
    for y in range(y0, y0 + h):
        rows.append(read_mem(0xA0000 + y*320 + x0, w))
    return rows

def is_piece_color(v):
    return 1 <= v <= 8 or 17 <= v <= 23

# 全棋盘扫描 (x 120..199, y 20..179): 方块+影子+暗色, 不含边框
def scan_board():
    rows = read_rows(20, 160, 120, 80)
    cells = []
    for i, row in enumerate(rows):
        y = 20 + i
        for x, v in enumerate(row):
            if is_piece_color(v):
                cells.append((120 + x, y, v))
    return cells

def bbox(cells):
    if not cells: return None
    xs = [c[0] for c in cells]; ys = [c[1] for c in cells]
    return (min(xs), min(ys), max(xs), max(ys))

results = []
def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}")

drain(2.5)
type_text("tetris\n")
time.sleep(1.5)

# 标题画面
rows = read_rows(60, 16, 80, 168)
sq = [rows[0][24*i] == 1+i for i in range(7)]
check("title squares", all(sq), str(sq))

send("sendkey spc", wait=0.2)     # 开始游戏
time.sleep(0.3)
send("sendkey p", wait=0.3)       # 暂停
b0 = bbox(scan_board())
print("initial bbox:", b0)
send("sendkey p", wait=0.2)       # 恢复

time.sleep(2.2)                   # 2.2s = 40 滴答 = 4 格
send("sendkey p", wait=0.3)
b1 = bbox(scan_board())
print("after 2.2s bbox:", b1)
send("sendkey p", wait=0.2)

fall = (b1[1] - b0[1]) if b0 and b1 else -1
check("gravity 4 cells (24..40px)", 24 <= fall <= 40, f"fall={fall}px")

send("sendkey left", wait=0.35)   # 左移
send("sendkey p", wait=0.3)
b2 = bbox(scan_board())
print("after left bbox:", b2)
send("sendkey p", wait=0.2)
left = (b0[0] - b2[0]) if b0 and b2 else -99
check("moved left 8px", left == 8, f"left={left}px")

send("sendkey p", wait=0.05)      # 恢复后立刻
send("sendkey down", wait=0.1)  # 软降 (窗口收紧, 避免混入重力)
send("sendkey p", wait=0.3)
b3 = bbox(scan_board())
print("after down bbox:", b3)
send("sendkey p", wait=0.2)
down = (b3[1] - b2[1]) if b2 and b3 else -99
check("soft drop 8px", down == 8, f"down={down}px")

b = read_mem(0xA0000 + 18*320 + 118, 1)
check("border color=9", b[0] == 9, f"pixel(118,18)={b[0]}")
rows = read_rows(36, 41, 216, 52)
npix = sum(1 for row in rows for v in row if 1 <= v <= 7)
check("NEXT preview drawn", npix > 20, f"{npix}px")

send("sendkey esc", wait=0.3)
time.sleep(1.5)
r = send("xp /1bx 0x449")
mode = [l for l in r.splitlines() if '0449:' in l]
ok = bool(mode) and '0x03' in mode[0]
check("back to text mode 03h", ok, mode[0].strip() if mode else "?")

# shell 回归: help
type_text("help\n")
time.sleep(0.6)
r = send("xp /1000wx 0xb8000", wait=0.05)
text = ""
for line in r.splitlines():
    if ':' not in line or '\x1b' in line: continue
    for tok in line.split(':', 1)[1].split():
        if tok.startswith('0x'):
            v = int(tok, 16)
            for sh in (0, 8, 16, 24):
                c = (v >> sh) & 0xFF
                if 32 <= c < 127: text += chr(c)
print("VGA text sample:", repr(text[text.find('Available'):text.find('Available')+120]))
check("shell works after game", "tetris" in text, f"len={len(text)}")

type_text("testmode\n")
time.sleep(1.2)
send("sendkey spc", wait=0.3)
time.sleep(1.0)
sock.close()
print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
