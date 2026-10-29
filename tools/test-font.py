#!/usr/bin/env python3
# 字体/字号里程碑测试: Terminus 字体加载 + font 命令 + 游戏返回后字体恢复
# 启动 QEMU (monitor socket + 串口重定向到文件), 全程无头验证。
import socket, subprocess, time, sys, os, re, threading

IMG = "build/os.img"
MON = "/tmp/newos-mon.sock"
SERIAL = "/tmp/newos-serial.log"
try: os.unlink(MON)
except FileNotFoundError: pass

# 串口经管道 + 读线程实时收集。
# ★两个坑: (1) QEMU stdio 直接重定向到文件会被缓冲等不到实时输出;
# (2) BufferedReader.read(4096) 在管道不足 4096 字节时会一直阻塞
# (启动日志才 ~1KB), 必须用 os.read 有数据就返回。
serial_file = open(SERIAL, "wb")
serial_buf = bytearray()
def serial_reader(pipe):
    fd = pipe.fileno()
    while True:
        d = os.read(fd, 65536)
        if not d: break
        serial_buf.extend(d)
        serial_file.write(d); serial_file.flush()

qemu = subprocess.Popen(
    ["qemu-system-i386", "-fda", IMG, "-snapshot", "-display", "none",
     "-monitor", f"unix:{MON},server,nowait", "-serial", "stdio"],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
# -snapshot: 镜像只读打开 + 内存覆盖层, 不拿写锁 —
# 孤儿 QEMU 不会让后续测试因 "Failed to get write lock" 假死
threading.Thread(target=serial_reader, args=(qemu.stdout,), daemon=True).start()

# 等 monitor socket 就绪
for _ in range(100):
    if os.path.exists(MON): break
    time.sleep(0.1)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MON); sock.settimeout(10)

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

def type_text(s, wait=0.12):
    for ch in s:
        if ch == ' ': send("sendkey spc", wait=wait)
        elif ch == '\n': send("sendkey ret", wait=wait)
        else: send(f"sendkey {ch}", wait=wait)

def serial_log():
    return serial_buf.decode(errors='replace')

# 串口基线: 每个测试步骤开始前 mark_serial_base(),
# 之后的检查只搜基线之后的新数据 (防止匹配到历史输出)。
serial_base = 0
def mark_serial_base():
    global serial_base
    serial_base = len(serial_buf)
def serial_since():
    return serial_log()[serial_base:]

def read_screen(nwords=1000):
    r = send(f"xp /{nwords}wx 0xb8000", wait=0.05)
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

def font_marker(h, scanline):
    # 串口自测行: [FONT] height=0x00000010 mismatch=0x00000000 scanline=0x0000000F vde=0x0000018F OK
    # (serial_put_hex 输出 8 位定宽十六进制)
    pat = rf"\[FONT\] height=0x{h:08X} mismatch=0x00000000 scanline=0x{scanline:08X} vde=0x[0-9A-F]+ OK"
    return re.search(pat, serial_since()) is not None

def wait_font_marker(h, scanline, timeout=5.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if font_marker(h, scanline): return True
        time.sleep(0.2)
    return False

def wait_for(cond, timeout=5.0, name=""):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if cond(): return True
        time.sleep(0.2)
    print(f"  [wait_for:{name}] timeout: buf={len(serial_buf)}B since={len(serial_since())}B tail={serial_log()[-100:]!r}", flush=True)
    return False

# ---------- 1. 启动: 自动加载 8x16 Terminus ----------
time.sleep(3.0)
dbg = serial_log()
check("boot: font16 loaded (plane2 readback + CRTC)",
      wait_font_marker(0x10, 0x0F, timeout=1.0))
# 等开机画面结束再检查 banner (banner 在 splash 之后才打印)
check("boot: splash done", wait_for(
    lambda: "[SP] done" in serial_log() or "[SP] skipped" in serial_log(),
    timeout=15.0, name="splash-done"))
time.sleep(1.0)
check("boot: banner on screen",
      wait_for(lambda: "MyQwen-OS" in read_screen(), timeout=5.0))
check("boot: no reset loop", dbg.count("[Paging] ENABLED") == 1,
      f"serial_buf={len(dbg)}B boots={dbg.count('[Paging] ENABLED')}")

# ---------- 2. font 14 ----------
mark_serial_base()
type_text("font 14\n")
check("font 14: hardware applied", wait_font_marker(0x0E, 0x0D))
check("font 14: screen works", "MyQwen-OS" in read_screen())
type_text("font\n")
time.sleep(0.5)
check("font 14: shell rows=28", "(28 lines)" in read_screen())

# ---------- 3. font 12 ----------
mark_serial_base()
type_text("font 12\n")
check("font 12: hardware applied", wait_font_marker(0x0C, 0x0B))
type_text("font\n")
time.sleep(0.5)
check("font 12: shell rows=33", "(33 lines)" in read_screen())

# ---------- 4. 非法参数 ----------
type_text("font 99\n")
time.sleep(0.5)
check("font 99: usage shown", "Usage: font 12 | 14 | 16" in read_screen())

# ---------- 5. 回到 16 + help 回归 ----------
mark_serial_base()
type_text("font 16\n")
check("font 16: hardware applied", wait_font_marker(0x10, 0x0F))
type_text("help\n")
time.sleep(0.6)
check("help: lists font command", "font N" in read_screen())

# ---------- 6. tetris 往返: 退出后字体自动恢复 ----------
# ★不用 BDA 轮询: monitor xp 风暴会冻结 VCPU (项目已知坑),
# 用内核自己的串口消息当退出信号, BDA 只在最后确认一次。
mark_serial_base()
type_text("tetris\n")
time.sleep(1.5)
send("sendkey spc", wait=0.3)
time.sleep(0.8)
send("sendkey esc", wait=0.3)
check("tetris roundtrip: exited", wait_for(lambda: "[GAME] Tetris exited" in serial_since(), timeout=10.0))
check("tetris roundtrip: font16 restored", wait_font_marker(0x10, 0x0F, timeout=8.0))
time.sleep(1.0)
r = send("xp /1bx 0x449")
check("tetris roundtrip: text mode back", '0x03' in r)

# ---------- 7. mario 往返 ----------
# ★标题画面是 "PRESS ANY KEY": 任意键都是"开始游戏" (ESC 也不例外!),
# 必须 spc 进游戏后再 ESC 退出 (同 test-mario.py 流程)。
mark_serial_base()
type_text("mario\n")
time.sleep(2.5)                      # 标题画面渲染慢 (TCG), 多等
send("sendkey spc", wait=0.5)        # 开始游戏
time.sleep(1.5)
send("sendkey esc", wait=0.3)        # 游戏内退出
check("mario roundtrip: exited", wait_for(lambda: "[GAME] Mario exited" in serial_since(), timeout=12.0, name="mario-exit"))
check("mario roundtrip: font16 restored", wait_font_marker(0x10, 0x0F, timeout=8.0))
time.sleep(1.0)
r = send("xp /1bx 0x449")
check("mario roundtrip: text mode back", '0x03' in r)

# ---------- 8. testmode 往返 (请求类型 0 路径回归) ----------
mark_serial_base()
type_text("testmode\n")
time.sleep(2.5)                      # 切 13h + 自测图形绘制需要时间
send("sendkey a", wait=0.3)
check("testmode: round trip passed",
      wait_for(lambda: "[RM] Text mode 03h restored, round trip OK!" in serial_since(), timeout=8.0))
check("testmode: font16 restored", wait_font_marker(0x10, 0x0F, timeout=3.0))

# ---------- 9. 游戏内字号变化后退出也能恢复 (font 12 → tetris → 回来仍是 12) ----------
mark_serial_base()
type_text("font 12\n")
check("step9: font 12 applied", wait_font_marker(0x0C, 0x0B))
mark_serial_base()
type_text("tetris\n")
time.sleep(1.5)
send("sendkey spc", wait=0.4)        # tetris 需要按键开始 (同 test-mario.py)
time.sleep(0.8)
send("sendkey esc", wait=0.3)
check("step9: tetris exited", wait_for(lambda: "[GAME] Tetris exited" in serial_since(), timeout=10.0, name="step9-tetris"))
check("font12 survives tetris", wait_font_marker(0x0C, 0x0B, timeout=3.0))
type_text("font\n")
time.sleep(0.5)
check("font12 survives tetris: rows=33", "(33 lines)" in read_screen())
mark_serial_base()
type_text("font 16\n")
check("step9: back to 16", wait_font_marker(0x10, 0x0F))

try:
    qemu.terminate()
except Exception:
    pass
qemu.wait(timeout=5)
print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
sys.exit(0 if all(ok for _, ok in results) else 1)
