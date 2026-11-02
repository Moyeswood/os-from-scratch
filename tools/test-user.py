#!/usr/bin/env python3
# 里程碑 10 测试: Ring 3 用户态
#   - 启动标记 ([GDT]/[USER]/无复位循环)
#   - run hello/count/crash/div0/echo/badptr 全流程 (串口预言机)
#   - 异常细节: #PF 错误码 U/W 位 + CR2=0xB8000, #DE, 杀掉后 shell 存活
#   - 屏幕验证: hello 输出渲染在 VGA 上
#   - 回归: tetris 实模式往返 (GDT 换表后 rm_entry 的 sgdt/lgdt 路径)
#   - 回归: font 切换 (与用户态无关但同跑一遍 GDT 新环境)
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
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}", flush=True)

def wait_for(cond, timeout=8.0, name=""):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if cond(): return True
        time.sleep(0.2)
    print(f"  [wait_for:{name}] timeout: tail={serial_log()[-160:]!r}", flush=True)
    return False

def wait_since(needle, timeout=8.0, name=""):
    return wait_for(lambda: needle in serial_since(), timeout, name)

# shell 存活预言机: 提示符只写屏幕不写串口, 用 pwd 的 [SH] 串口标记验证
def check_shell_alive(name):
    mark_serial_base()
    type_text("pwd\n")
    return check(name, wait_since("[SH] pwd:", name=name))

# ================= 1. 启动 =================
time.sleep(3.0)
dbg = serial_log()
check("boot: GDT loaded (user segs + TSS)", "[GDT] kernel GDT" in dbg)
check("boot: user region mapped", "[USER] user region 0x02000000..0x02400000" in dbg)
check("boot: splash done", wait_for(
    lambda: "[SP] done" in serial_log() or "[SP] skipped" in serial_log(),
    timeout=15.0, name="splash-done"))
time.sleep(1.0)
check("boot: banner on screen", wait_for(
    lambda: "MyQwen-OS" in read_screen(), timeout=5.0, name="banner"))
check("boot: no reset loop", dbg.count("[Paging] ENABLED") == 1)

# ================= 2. run hello =================
mark_serial_base()
type_text("run hello\n")
check("hello: user output on serial",
      wait_since("Hello from Ring 3!", name="hello-out"))
check("hello: exit code 0", wait_since("[USER] exit code=0x00000000", name="hello-exit"))
time.sleep(0.6)
check("hello: rendered on screen", "Hello from Ring 3!" in read_screen())
# 进程输出后 shell 提示符还在屏幕上
check("hello: shell prompt back", "OS:/>" in read_screen() or "OS:/" in read_screen())

# ================= 3. run count =================
mark_serial_base()
type_text("run count\n")
check("count: 1..15 printed", wait_since("count: ticks after", 30.0, "count-run"))
check("count: all numbers", all(f"{n} " in serial_since() for n in range(1, 16)))
check("count: ticks advanced in user mode",
      wait_since("timer kept ticking", name="count-ticks"))

# ================= 4. run crash (#PF) =================
mark_serial_base()
type_text("run crash\n")
check("crash: #14 Page Fault", wait_since("[EXC] #14 Page Fault", name="crash-pf"))
check("crash: U/S violation bit", wait_since("user access to supervisor page", name="crash-usr"))
check("crash: CR2 = 0xB8000", wait_since("CR2=0x000B8000", name="crash-cr2"))
check("crash: killed", wait_since("[USER] process killed", name="crash-kill"))
check_shell_alive("crash: shell alive after")

# ================= 5. run div0 (#DE) =================
mark_serial_base()
type_text("run div0\n")
check("div0: #00 Divide Error", wait_since("[EXC] #00 Divide Error", name="div0-de"))
check("div0: killed", wait_since("[USER] process killed", name="div0-kill"))
check_shell_alive("div0: shell alive after")

# ================= 6. run echo =================
mark_serial_base()
type_text("run echo\n")
check("echo: prompt", wait_since("type anything", name="echo-start"))
time.sleep(0.5)
send("sendkey h", wait=0.4); send("sendkey i", wait=0.4)
time.sleep(1.0)
check("echo: keys echoed", "hi" in serial_since())
send("sendkey esc", wait=0.5)
check("echo: bye on ESC", wait_since("echo: bye!", name="echo-bye"))
check("echo: exit code 0", wait_since("[USER] exit code=0x00000000", name="echo-exit"))
check_shell_alive("echo: shell alive after")

# ================= 7. run badptr =================
mark_serial_base()
type_text("run badptr\n")
check("badptr: kernel rejected pointer",
      wait_since("[SYS] puts: rejected kernel pointer", name="badptr-rej"))
check("badptr: program saw rejection", wait_since("kernel rejected the pointer (good!)", name="badptr-msg"))
check("badptr: exit code 0", wait_since("[USER] exit code=0x00000000", name="badptr-exit"))

# ================= 8. 参数与错误处理 =================
mark_serial_base()
type_text("run\n")
time.sleep(0.6)
check("run no-arg: usage shown", "Usage: run" in read_screen())
mark_serial_base()
type_text("run nosuch\n")
check("run nosuch: unknown program", wait_since("[USER] unknown program: nosuch", name="badname"))
check_shell_alive("run nosuch: shell alive")

# ================= 9. 回归: tetris 实模式往返 (GDT 已换表) =================
# rm_entry.asm 用 sgdt 动态保存当前内核 GDTR, 换表后应仍能往返
mark_serial_base()
type_text("tetris\n")
time.sleep(1.5)
send("sendkey spc", wait=0.4)
time.sleep(0.8)
send("sendkey esc", wait=0.3)
check("regress: tetris exited", wait_since("[GAME] Tetris exited", 10.0, "tetris-exit"))
check("regress: text mode restored", wait_for(
    lambda: "[GAME] Text mode restored" in serial_since(), 8.0, "tetris-mode"))
check_shell_alive("regress: shell alive")

# ================= 10. 回归: font 切换 (同环境冒烟) =================
mark_serial_base()
type_text("font 16\n")
check("regress: font16 applied", wait_for(
    lambda: re.search(r"\[FONT\] height=0x00000010 mismatch=0x00000000", serial_since()),
    8.0, "font16"))
type_text("font 18\n")
check("regress: font18 back to default", wait_for(
    lambda: re.search(r"\[FONT\] height=0x00000012 mismatch=0x00000000", serial_since()),
    8.0, "font18"))

try:
    qemu.terminate()
except Exception:
    pass
qemu.wait(timeout=5)
print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
sys.exit(0 if all(ok for _, ok in results) else 1)
