#!/usr/bin/env python3
# c4 迷你 C 编译器测试: 编辑器写源码 → cc 编译 → 运行 → 串口验证输出
# 覆盖: hello/循环求和/递归/指针+malloc+字符串/argv/文件系统 open-read/
# 编译错误/文件缺失/无 main/ESC 中断/回归
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
def type_text(s, wait=0.18):
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

# 用编辑器写一个 C 源文件 (多行: 每行 + 回车)
def write_source(path, lines):
    mark_serial_base()
    type_text(f"edit {path}\n")
    ok = wait_for(lambda: f"[ED] new: {path}" in serial_since()
                        or f"[ED] open: {path}" in serial_since()
                        or "[ED] fail" in serial_since(),
                  timeout=8.0, name=f"edit-{path}")
    time.sleep(1.2)              # kbd_drain 窗口
    if not ok: return False
    for i, line in enumerate(lines):
        type_text(line, wait=0.15)
        if i < len(lines) - 1:
            press("ret", wait=0.3)
    press("ctrl-s", wait=0.4)
    time.sleep(0.4)
    press("ctrl-x", wait=0.4)
    wait_for(lambda: "[ED] exit: saved" in serial_since(), timeout=6.0, name="ed-exit")
    time.sleep(0.8)
    return True

# 编译并运行, 等指定串口文本 (程序输出或错误消息 — 错误路径不打印 exit())
def run_cc(path, expect, timeout=10.0, name=""):
    mark_serial_base()
    type_text(f"cc {path}\n")
    ok = wait_for(lambda: expect in serial_since(), timeout=timeout, name=name or f"cc-{path}")
    time.sleep(0.6)
    return ok

# ---------- 1. 启动 ----------
time.sleep(3.5)
dbg = serial_log()
check("boot: fs ready", "[FS] selftest PASS" in dbg)
check("boot: no reset loop", dbg.count("[Paging] ENABLED") == 1,
      f"boots={dbg.count('[Paging] ENABLED')}")
# 等开机画面结束 (任意键不会提前到 — 本套件在画面结束后才敲键盘)
check("boot: splash done", wait_for(
    lambda: "[SP] done" in serial_log() or "[SP] skipped" in serial_log(),
    timeout=15.0, name="splash-done"))
time.sleep(1.0)

# ---------- 2. hello world (编辑器 → cc 完整闭环) ----------
check("write hello.c", write_source("/hello.c", [
    'int main() { printf("hello from C! (%d)\\n", 42); return 0; }']))
check("cc hello.c", run_cc("/hello.c", "hello from C! (42)", name="cc-hello"))

# ---------- 3. 循环求和 ----------
check("write sum.c", write_source("/sum.c", [
    "int main() { int i; int s; s = 0; i = 0;",
    "while (i < 10) { s = s + i; i = i + 1; }",
    'printf("sum=%d\\n", s); return 0; }']))
check("cc sum.c = 45", run_cc("/sum.c", "sum=45", name="cc-sum"))

# ---------- 4. 递归 fib ----------
check("write fib.c", write_source("/fib.c", [
    "int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }",
    'int main() { printf("fib(10)=%d\\n", fib(10)); return 0; }']))
check("cc fib.c = 55", run_cc("/fib.c", "fib(10)=55", name="cc-fib"))

# ---------- 5. 指针 + malloc + 字符串 + sizeof ----------
check("write str.c", write_source("/str.c", [
    "int main() { char *s; s = malloc(16);",
    "s[0] = 'h'; s[1] = 'i'; s[2] = 0;",
    'printf("%s %d\\n", s, sizeof(int)); return 0; }']))
check("cc str.c", run_cc("/str.c", "hi 4", name="cc-str"))

# ---------- 6. argv 传递 ----------
check("write argv.c", write_source("/argv.c", [
    'int main(int argc, char **argv) { printf("argc=%d argv1=%s\\n", argc, argv[1]); return 0; }']))
check("cc argv.c hello world", run_cc("/argv.c hello world", "argc=3 argv1=hello", name="cc-argv"))

# ---------- 7. 文件系统集成: C 程序读 OS 里的文件 ----------
run_cmd('echo file data here > /data.txt', "[SH] echo ok", name="echo-data")
check("write fileread.c", write_source("/fileread.c", [
    "int main() { int fd; char *buf; int n;",
    'fd = open("/data.txt", 0); buf = malloc(32);',
    "n = read(fd, buf, 31); buf[n] = 0;",
    'printf("read %d: %s\\n", n, buf); return 0; }']))
check("cc fileread.c", run_cc("/fileread.c", "read 15: file data here", name="cc-fileread"))

# ---------- 8. 编译错误 ----------
check("write bad.c", write_source("/bad.c", [
    'int main() { printf("x" }']))
check("cc bad.c → compile error", run_cc("/bad.c", "bad expression", name="cc-bad"))
check("shell alive after error", run_cmd("pwd", "[SH] pwd: /", name="pwd-after-err"))

# ---------- 9. 文件缺失 / 无 main ----------
check("cc missing → error", run_cc("/nope.c", "could not open", name="cc-missing"))
check("write nomain.c", write_source("/nomain.c", ["int x;"]))
check("cc nomain → error", run_cc("/nomain.c", "main() not defined", name="cc-nomain"))

# ---------- 10. ESC 中断失控程序 ----------
check("write loop.c", write_source("/loop.c", [
    "int main() { while (1) { } return 0; }"]))
mark_serial_base()
type_text("cc /loop.c\n")
time.sleep(2.0)
press("esc", wait=0.4)
check("cc loop → ESC interrupt", wait_for(
    lambda: "interrupted at cycle" in serial_since(), timeout=6.0, name="cc-interrupt"))
time.sleep(0.6)
check("shell alive after interrupt", run_cmd("pwd", "[SH] pwd: /", name="pwd-after-int"))

# ---------- 11. 回归 ----------
check("regression: edit opens", run_cmd("edit /hello.c", "[ED] open: /hello.c", name="reg-edit"))
time.sleep(1.2)
press("ctrl-x", wait=0.4)
type_text("\n")
time.sleep(0.8)
check("regression: font 12", run_cmd("font 12",
      "[FONT] height=0x0000000C mismatch=0x00000000 scanline=0x0000000B vde=0x", name="reg-font12"))
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
