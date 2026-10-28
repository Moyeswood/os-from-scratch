#!/usr/bin/env python3
# 超级马里奥集成测试 v2: 暂停后等渲染收敛再扫描 (渲染比游戏状态慢几个滴答)
import socket, sys, time

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(sys.argv[1]); sock.settimeout(10)

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

def pixel(x, y):
    return read_mem(0xA0000 + y*320 + x, 1)[0]

def scan_color(color):
    fb = read_mem(0xA0000, 64000)
    minx = miny = 9999; maxx = maxy = -1
    for i, v in enumerate(fb):
        if v == color:
            x = i % 320; y = i // 320
            minx = min(minx, x); maxx = max(maxx, x)
            miny = min(miny, y); maxy = max(maxy, y)
    return None if maxx < 0 else (minx, miny, maxx, maxy)

results = []
def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}")

def freeze_scan(settle=0.9, retry=0):
    # 暂停 → 等渲染收敛 → 扫描; retry>0 时若扫不到 (无敌帧闪烁) 就重试
    send("sendkey p", wait=0.3)
    time.sleep(settle)
    b = scan_color(18)
    for _ in range(retry):
        if b is not None: break
        send("sendkey p", wait=0.15)   # 短暂恢复几滴答换闪烁相位
        time.sleep(0.3)
        send("sendkey p", wait=0.3)
        time.sleep(0.6)
        b = scan_color(18)
    send("sendkey p", wait=0.2)
    return b

drain(2.5)
type_text("mario\n")
time.sleep(1.5)

# --- 标题画面 ---
check("title: sky color=1", pixel(10, 100) == 1)
check("title: ground drawn", pixel(5, 190) in (4, 5))
check("title: mario sprite", pixel(122, 168) == 18)

send("sendkey spc", wait=0.4)      # 开始游戏
time.sleep(0.4)
b0 = freeze_scan()
print("spawn bbox:", b0)
check("game: mario at spawn", b0 and b0[0] < 50 and b0[1] > 160, str(b0))

# --- 向右跑 1.2 秒 ---
send("sendkey d 1200", wait=0.3)
b1 = freeze_scan()
print("after run bbox:", b1)
run = (b1[0] - b0[0]) if b0 and b1 else -1
check("run right ~54px", 45 <= run <= 65, f"run={run}px")

# --- 跳跃 (扫描后先等游戏时钟同步, 再采样两个时刻) ---
time.sleep(1.5)                    # 上次扫描的沉降
send("sendkey spc", wait=0.4)
bj1 = freeze_scan()
print("airborne-1 bbox:", bj1)
send("sendkey spc", wait=0.8)
bj2 = freeze_scan()
print("airborne-2 bbox:", bj2)
bj = bj1 if (bj1 and bj1[1] < 145) else bj2
print("airborne bbox:", bj)
check("jump: airborne", bj and bj[1] < 140, str(bj))

time.sleep(3.0)
bl = freeze_scan()
print("landed bbox:", bl)
check("jump: landed", bl and bl[1] >= 160, str(bl))

# --- 掉坑死亡重生 (无敌帧 90 滴答闪烁, 多等一会) ---
send("sendkey d 5000", wait=4.0)   # 按住右 5 秒 → 跑进第一个坑
time.sleep(9.0)                    # 死亡动画 + 重生 + 无敌帧 (90 滴答) 结束
br = freeze_scan(retry=3)          # 闪烁重试
print("after pit bbox:", br)
check("pit death: respawn at start", br and br[0] < 60, str(br))

# --- ESC 退出 (轮询 BDA 直到 0x03) ---
send("sendkey esc", wait=0.3)
mode = None
for _ in range(10):
    time.sleep(0.5)
    r = send("xp /1bx 0x449")
    lines = [l for l in r.splitlines() if '0449:' in l]
    if lines:
        mode = lines[0].split(':')[1].strip()
        if '0x03' in mode: break
check("ESC back to text mode", mode is not None and '0x03' in mode, f"mode={mode}")

# --- shell 回归: help 里有 mario ---
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
check("shell works (help lists mario)", "mario" in text, f"len={len(text)}")

# --- tetris 回归 ---
type_text("tetris\n")
time.sleep(1.5)
send("sendkey spc", wait=0.3)
time.sleep(0.8)
send("sendkey esc", wait=0.3)
mode = None
for _ in range(10):
    time.sleep(0.5)
    r = send("xp /1bx 0x449")
    lines = [l for l in r.splitlines() if '0449:' in l]
    if lines:
        mode = lines[0].split(':')[1].strip()
        if '0x03' in mode: break
check("tetris regression", mode is not None and '0x03' in mode, f"mode={mode}")

sock.close()
print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
