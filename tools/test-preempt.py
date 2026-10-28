#!/usr/bin/env python3
# 抢占式多任务测试: start 后恶霸任务 C (从不 yield) 与其他任务共存
import socket, sys, time

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(sys.argv[1]); sock.settimeout(8)

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

def vga_text():
    r = send("xp /1000wx 0xb8000", wait=0.02)
    text = ""
    for line in r.splitlines():
        if ':' not in line or '\x1b' in line: continue
        for tok in line.split(':', 1)[1].split():
            if tok.startswith('0x'):
                v = int(tok, 16)
                for sh in (0, 8, 16, 24):
                    c = (v >> sh) & 0xFF
                    if 32 <= c < 127: text += chr(c)
    return text

results = []
def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}")

drain(2.5)

# 0. 启动 sanity
type_text("help\n")
time.sleep(0.5)
t = vga_text()
check("boot+shell OK", "Available" in t)

# 1. start → 恶霸 C 上场
type_text("start\n")
time.sleep(10.0)
t = vga_text()
na, nb, nc = t.count('A'), t.count('B'), t.count('C')
check("A/B continue while C hogs", na >= 10 and nb >= 10, f"A={na} B={nb} C={nc}")
check("hog C got CPU", nc >= 2, f"C={nc}")

# 2. ps 在 start 期间可用
type_text("ps\n")
time.sleep(0.8)
t = vga_text()
check("ps works during start", "task_c" in t, "")

# 3. stop → 画面静止
type_text("stop\n")
time.sleep(1.0)
t1 = vga_text()
c1 = (t1.count('A'), t1.count('B'), t1.count('C'))
time.sleep(2.0)
t2 = vga_text()
c2 = (t2.count('A'), t2.count('B'), t2.count('C'))
check("stop freezes demo", c1 == c2, f"{c1} -> {c2}")

# 4. tetris 回归 (抢占调度不影响游戏)
type_text("tetris\n")
time.sleep(1.5)
send("sendkey spc", wait=0.3)
time.sleep(1.0)
send("sendkey esc", wait=0.3)
time.sleep(1.5)
r = send("xp /1bx 0x449")
mode = [l for l in r.splitlines() if '0449:' in l]
ok = bool(mode) and '0x03' in mode[0]
check("tetris regression", ok, mode[0].strip() if mode else "?")

# 5. testmode 回归
type_text("testmode\n")
time.sleep(1.2)
send("sendkey spc", wait=0.3)
time.sleep(1.0)
sock.close()
print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
