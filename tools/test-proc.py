#!/usr/bin/env python3
# 里程碑 11b 测试: 多进程 — 独立地址空间 + fork + 后台进程 + kill
#   - run hello 前台 (新架构下的旧流程, 格式兼容)
#   - run forkdemo: fork 出父子两个并发进程, 地址空间隔离断言
#     (子进程改 counter=200 之后, 父进程的 counter 仍是 100)
#   - run spinner &: 后台进程, 期间 shell 可用 (pwd 能执行)
#   - ps: spinner 进程可见 (RUNNING/SLEEPING)
#   - kill <pid>: 后台进程被杀, 打点停止, 内存由下次 spawn 清扫
#   - run crash &: 后台进程异常被杀, shell 存活, 槽位可复用
import socket, subprocess, time, sys, os, re, threading

IMG = "build/os.img"
MON = "/tmp/newos-proc-mon.sock"
SERIAL = "/tmp/newos-proc-serial.log"
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
    # 先排空上次超时留下的残留响应: 否则 stale "(qemu)" 会让本次
    # 提前返回, 测试节奏和监视器处理节奏脱钩
    # (排空阶段用短超时: 空闲 socket 上不能白等)
    sock.settimeout(0.3)
    try:
        while True:
            d = sock.recv(65536)
            if not d: break
    except socket.timeout: pass
    sock.settimeout(10)
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
        elif ch == '&': send("sendkey shift-7", wait=wait)
        else: send(f"sendkey {ch}", wait=wait)

def serial_log():
    return serial_buf.decode(errors='replace')

serial_base = 0
def mark_serial_base():
    global serial_base
    # 留 64 字节余量: 串口按块到达, 标记行可能在 mark 时刻正被读入,
    # 窗口起点恰好切在行中间会把 "[PROC] xxx" 切成 "ROC] xxx"
    serial_base = max(0, len(serial_buf) - 64)
def serial_since():
    return serial_log()[serial_base:]

def read_screen(nwords=1600):
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

def wait_for(cond, timeout=10.0, name=""):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if cond(): return True
        time.sleep(0.2)
    # 超时前最后一查: 覆盖"标记在最后一次 sleep 期间到达"的窗口
    if cond(): return True
    print(f"  [wait_for:{name}] timeout: tail={serial_log()[-200:]!r}", flush=True)
    return False

def wait_since(needle, timeout=10.0, name=""):
    return wait_for(lambda: needle in serial_since(), timeout, name)

# ================= 1. 启动 =================
time.sleep(3.0)
dbg = serial_log()
check("boot: scheduler marker", "[TASK] scheduler" in dbg)
check("boot: splash done", wait_for(
    lambda: "[SP] done" in serial_log() or "[SP] skipped" in serial_log(),
    timeout=15.0, name="splash-done"))
time.sleep(1.0)

# ================= 2. run hello 前台 (新架构下的旧流程) =================
mark_serial_base()
type_text("run hello\n")
check("hello: output", wait_since("Hello from Ring 3!", name="hello-out"))
check("hello: spawn marker", "[PROC] spawn hello" in serial_since())
check("hello: exit code", wait_since("[USER] exit code=0x00000000", name="hello-exit"))
check("hello: proc exited marker", "[PROC] pid" in serial_since()
      and "exited code=0x00000000" in serial_since())
check("hello: rendered", "Hello from Ring 3!" in read_screen())

# ================= 3. run forkdemo: fork + 地址空间隔离 =================
mark_serial_base()
type_text("run forkdemo\n")
check("fork: child starts with counter=100",
      wait_since("child: pid=", name="fork-child") and "counter=100" in serial_since())
check("fork: child sets own copy to 200", wait_since("child: set counter=200", name="fork-set"))
check("fork: parent sees child pid", wait_since("parent: forked pid=", name="fork-parent"))
# ★核心断言: 子进程把 counter 改成 200 后, 父进程的第一个 counter 仍是 100
#   (同一份代码、同一个变量名, 各自的内存 — 独立页目录的意义)
check("fork: parent counter untouched by child",
      wait_since("parent: counter=100", name="fork-isol"))
s = serial_since()
i_set = s.find("child: set counter=200")
i_p100 = s.find("parent: counter=100")
check("fork: ordering set-before-parent-print", 0 <= i_set < i_p100,
      f"set@{i_set} parent@{i_p100}")
# 父子交错推进 (并发): 等两边跑完全部 4 次循环 (循环体 sleep 4 滴答 ≈ 0.9s)
check("fork: child advances 200..203", wait_for(
    lambda: all(f"child: counter={n}" in serial_since() for n in (200, 201, 202, 203)),
    15.0, "fork-child-advance"))
check("fork: parent advances 100..103", wait_for(
    lambda: all(f"parent: counter={n}" in serial_since() for n in (100, 101, 102, 103)),
    15.0, "fork-parent-advance"))
check("fork: both exit", wait_since("parent: exiting", 20.0, "fork-exit")
      and "[PROC] pid" in serial_since())
check("fork: foreground exit code", wait_since("[USER] exit code=0x00000000", name="fork-fg-exit"))

# ================= 4. run spinner &: 后台进程 =================
mark_serial_base()
type_text("run spinner &\n")
check("bg: spawn marker", wait_since("[PROC] spawn spinner", name="bg-spawn"))
check("bg: run marker", wait_since(") in background", name="bg-run"))
m = re.search(r"spinner \(pid=0x([0-9A-F]+)\)", serial_since())
check("bg: pid reported", bool(m))
sp_pid = int(m.group(1), 16) if m else 0

# 打点在串口流动 (spinner 每 2 滴答一个点)
n0 = len(serial_buf)
time.sleep(1.5)
n1 = len(serial_buf)
check("bg: dots flowing", n1 - n0 >= 5, f"+{n1 - n0} bytes")

# 后台期间 shell 可用
mark_serial_base()
type_text("pwd\n")
check("bg: shell usable while spinner runs", wait_since("[SH] pwd:", name="bg-pwd"))

# ps 显示 spinner
type_text("ps\n")
time.sleep(0.8)
t = read_screen()
check("bg: ps shows spinner", "spinner" in t)
check("bg: ps shows its state", "RUNNING" in t or "SLEEPING" in t)

# ================= 5. kill =================
mark_serial_base()
type_text(f"kill {sp_pid}\n")
check("kill: killed marker", wait_since("killed by kill command", name="kill-marker"))
time.sleep(0.5)
n2 = len(serial_buf)
time.sleep(1.5)
n3 = len(serial_buf)
check("kill: dots stopped", n3 == n2, f"+{n3 - n2} bytes after kill")

# ps: ZOMBIE (被 kill 的进程等下一次 spawn 清扫)
type_text("ps\n")
time.sleep(0.8)
t = read_screen()
check("kill: ps shows ZOMBIE", "ZOMBIE" in t)

# ================= 6. run crash &: 后台异常被杀 =================
mark_serial_base()
type_text("run crash &\n")
check("crash-bg: #14 Page Fault", wait_since("[EXC] #14 Page Fault", name="cbg-pf"))
check("crash-bg: killed by exception", wait_since("killed by exception", name="cbg-kill"))
# ★QEMU 11.1.2 仿真怪癖: 用户态异常杀进程后紧跟着的串口输出会在
# 仿真层被缓冲, 直到后续串口输出把它"挤"出来 (内核已验证正确 —
# 零延时内存日志证明 shell 收到按键并执行了命令; 详见
# note/多进程开发记录.md 的排查记录)。这里用屏幕验证 shell 存活
# (xp 直读显存, 不经串口), 串口断言顺延到下一段一起验。
type_text("df\n")
check("crash-bg: shell executes df after kill", wait_since("[SH] df:", name="cbg-df"))
time.sleep(0.5)
t = read_screen()
check("crash-bg: shell alive (screen shows df output)", "Filesystem:" in t)

# ================= 7. 槽位复用 (清扫孤儿僵尸 + 空槽) =================
mark_serial_base()
type_text("run hello\n")
check("reuse: hello runs again (zombie swept + slot reused)",
      wait_since("Hello from Ring 3!", name="reuse-hello"))
check("reuse: orphan zombie swept marker", "[PROC] swept orphan zombie" in serial_since())

# ================= 8. 双后台并发 =================
mark_serial_base()
type_text("run spinner &\n", wait=0.3)
type_text("run count &\n", wait=0.3)
# 两个 spawn 标记可能随键入陆续到达 (shell 一边收键一边执行), 等齐
check("dual: both spawned", wait_for(
    lambda: serial_since().count("[PROC] spawn") == 2, 15.0, "dual-spawns"))
check("dual: count exits in background",
      wait_since("exited code=0x00000000", 30.0, "dual-count"))
# count 退出后 spinner 还在打点, shell 还能用
mark_serial_base()
type_text("pwd\n")
check("dual: shell alive with spinner still running", wait_since("[SH] pwd:", name="dual-pwd"))

try:
    qemu.terminate()
except Exception:
    pass
qemu.wait(timeout=5)
print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
sys.exit(0 if all(ok for _, ok in results) else 1)
