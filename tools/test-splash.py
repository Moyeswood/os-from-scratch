#!/usr/bin/env python3
# 开机画面测试: 启动自动播放 / 任意键跳过 / splash 命令重放 / 回归
import socket, subprocess, time, sys, os, re, threading

IMG = "build/os.img"
MON = "/tmp/newos-mon.sock"
SERIAL = "/tmp/newos-serial.log"
try: os.unlink(MON)
except FileNotFoundError: pass

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
threading.Thread(target=serial_reader, args=(qemu.stdout,), daemon=True).start()

for _ in range(100):
    if os.path.exists(MON): break
    time.sleep(0.05)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MON); sock.settimeout(10)

def drain_greeting():
    time.sleep(0.5)
    try:
        while True:
            d = sock.recv(65536)
            if not d: break
            if d.rstrip().endswith(b"(qemu)"): break
    except socket.timeout: pass
drain_greeting()

def send(cmd, wait=0.1):
    if isinstance(cmd, str): cmd = cmd.encode()
    sock.sendall(cmd + b"\n")
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

KEYMAP = {
    ' ': 'spc', '\n': 'ret', '/': 'slash', '.': 'dot', ',': 'comma',
    '-': 'minus', '=': 'equal', '>': 'shift-dot', '<': 'shift-comma',
    '_': 'shift-minus', '+': 'shift-equal', '!': 'shift-1', '"': 'shift-apostrophe',
    "'": 'apostrophe', ';': 'semicolon', ':': 'shift-semicolon',
    '?': 'shift-slash', '(': 'shift-9', ')': 'shift-0', '|': 'shift-backslash',
    '\\': 'backslash', '*': 'shift-8', '&': 'shift-7', '%': 'shift-5',
    '^': 'shift-6', '$': 'shift-4', '#': 'shift-3', '@': 'shift-2',
    '~': 'shift-grave_accents', '`': 'grave_accents', '[': 'bracket_left',
    ']': 'bracket_right', '{': 'shift-bracket_left', '}': 'shift-bracket_right',
}
KEYMAP.update({chr(c): f"shift-{chr(c + 32)}" for c in range(ord('A'), ord('Z') + 1)})
def type_text(s, wait=0.2):
    for ch in s:
        send(f"sendkey {KEYMAP.get(ch, ch)}", wait=wait)
def press(key, wait=0.3):
    send(f"sendkey {key}", wait=wait)

def serial_log():
    return serial_buf.decode(errors='replace')

serial_base = 0
def mark_serial_base():
    global serial_base
    serial_base = len(serial_buf)
def serial_since():
    return serial_log()[serial_base:]

results = []
def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}")

def wait_for(cond, timeout=8.0, name=""):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if cond(): return True
        time.sleep(0.2)
    print(f"  [wait:{name}] timeout, serial tail: {serial_since()[-120:]!r}", flush=True)
    return False

def run_cmd(cmdline, marker, timeout=8.0, name=""):
    mark_serial_base()
    type_text(cmdline + "\n")
    ok = wait_for(lambda: marker in serial_since(), timeout=timeout, name=name or cmdline)
    time.sleep(0.6)
    return ok

# ---------- 1. 启动: 开机画面自动播放 ----------
time.sleep(3.0)
dbg = serial_log()
check("boot: fs ready", "[FS] selftest PASS" in dbg)
check("boot: splash starts", "[SP] splash" in dbg)
check("boot: splash finishes", wait_for(
    lambda: "[SP] done" in serial_log() or "[SP] skipped" in serial_log(),
    timeout=15.0, name="boot-splash"))
time.sleep(1.0)
check("boot: shell up (pwd works)", run_cmd("pwd", "[SH] pwd: /", name="pwd-boot"))

# ---------- 2. splash 命令重放 ----------
check("splash command", run_cmd("splash", "[SP] splash", name="cmd-splash"))
check("splash command finishes", wait_for(
    lambda: "[SP] done" in serial_since(), timeout=15.0, name="cmd-splash-done"))
time.sleep(0.8)
check("shell alive after replay", run_cmd("pwd", "[SH] pwd: /", name="pwd-replay"))

# ---------- 3. 任意键跳过 ----------
mark_serial_base()
type_text("splash\n")
wait_for(lambda: "[SP] splash" in serial_since(), timeout=8.0, name="skip-start")
time.sleep(1.0)                        # 动画进行中
press("a", wait=0.3)                   # 任意键
check("key skips splash", wait_for(
    lambda: "[SP] skipped" in serial_since(), timeout=8.0, name="skip-done"))
time.sleep(0.8)
# 跳过键不能污染 shell 输入: pwd 必须干净
check("skip key not in input", run_cmd("pwd", "[SH] pwd: /", name="pwd-skip"))

# ---------- 4. 回归 ----------
check("regression: mkdir", run_cmd("mkdir sp-regress", "[SH] mkdir ok", name="reg-mkdir"))
check("regression: font 14", run_cmd("font 14",
      "[FONT] height=0x0000000E mismatch=0x00000000 scanline=0x0000000D vde=0x", name="reg-font14"))
check("regression: font 16", run_cmd("font 16",
      "[FONT] height=0x00000010 mismatch=0x00000000 scanline=0x0000000F vde=0x", name="reg-font16"))
mark_serial_base()
type_text("tetris\n")
time.sleep(1.5)
press("spc", wait=0.4)
time.sleep(0.8)
press("esc", wait=0.3)
check("regression: tetris exits", wait_for(
    lambda: "[GAME] Tetris exited" in serial_since(), timeout=10.0, name="reg-tetris"))

try:
    qemu.terminate()
except Exception:
    pass
qemu.wait(timeout=5)
print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
sys.exit(0 if all(ok for _, ok in results) else 1)
