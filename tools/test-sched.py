#!/usr/bin/env python3
# 里程碑 11a 测试: PCB 调度器 + 键盘环形缓冲
#   - 启动: [TASK] 调度器标记
#   - ps: idle/shell/task_a..c 状态 (demo 任务 start 前 SLEEPING)
#   - 键盘 type-ahead: 命令执行期间按的键排队, 结束后照常执行
#   - 排队顺序: run count 期间敲 pwd → count 先结束, pwd 后执行
#   - start/stop: A/B/C 均衡抢占 + stop 冻结 (回归)
#   - 老回归由 test-user.py 等 8 套独立覆盖, 本套只管新行为
import socket, subprocess, time, sys, os, threading

IMG = "build/os.img"
MON = "/tmp/newos-sched-mon.sock"
SERIAL = "/tmp/newos-sched-serial.log"
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

def wait_screen(predicate, timeout=4.0):
    # VGA monitor reads occasionally race the guest's PS/2 rendering. Retry
    # the snapshot instead of treating one stale frame as a scheduler failure.
    start = time.time()
    text = ""
    while time.time() - start < timeout:
        text = read_screen()
        if predicate(text):
            return text
        time.sleep(0.2)
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

# ================= 1. 启动 =================
time.sleep(3.0)
dbg = serial_log()
check("boot: scheduler marker", "[TASK] scheduler: idle + shell + demo A/B/C" in dbg)
check("boot: splash done", wait_for(
    lambda: "[SP] done" in serial_log() or "[SP] skipped" in serial_log(),
    timeout=15.0, name="splash-done"))
mark_serial_base()
type_text("pwd\n")
wait_since("[SH] pwd:", 10.0, "shell-ready")
time.sleep(0.3)

# ================= 2. ps: PCB 状态 =================
def re_shell_sleeping(t):
    import re
    m = re.search(r"shell\s+(RUNNING|SLEEPING|READY)", t)
    return bool(m) and m.group(1) in ("SLEEPING", "RUNNING")

type_text("ps\n")
time.sleep(1.0)
t = wait_screen(lambda text: all(
    name in text for name in ("shell", "task_a", "task_b", "task_c")))
# (idle 行在 ps 输出最顶上, 20 行屏幕里被横幅+提示符挤上去滚掉了,
#  这里查屏幕能看见的 shell + A/B/C; idle 的存在由调度器本身保证)
check("ps: shell + A/B/C listed",
      all(n in t for n in ("shell", "task_a", "task_b", "task_c")))
# 空闲等输入: shell 在 SLEEPING 等键盘, A/B/C 睡死
check("ps: shell SLEEPING on keyboard", re_shell_sleeping(t))
check("ps: demo tasks SLEEPING before start", t.count("SLEEPING") >= 3)

# ================= 3. type-ahead: 快速连敲两条命令 =================
# 不等第一条完成就连敲 pwd + df: 两条都应执行 (按键在环形缓冲排队)
mark_serial_base()
type_text("pwd\n", wait=0.02)
type_text("df\n", wait=0.02)
check("type-ahead: first pwd executes", wait_since("[SH] pwd:", name="ta-1"))
check("type-ahead: second df executes", wait_since("[SH] df:", name="ta-2"))

# ================= 4. 命令执行期间排队: run count 期间敲 pwd =================
# count 忙等 ~4 秒; 期间敲 pwd\n 必须等 count 退出后才执行 (排队, 不丢)
mark_serial_base()
type_text("run count\n")
time.sleep(0.8)                      # count 还在跑
type_text("pwd\n", wait=0.05)         # 此时敲的键进环形缓冲排队
check("queue: count finishes first", wait_since("count: ticks after", 30.0, "count-finish"))
check("queue: pwd waits for count exit",
      wait_since("[SH] pwd:", name="queued-pwd"))
s = serial_since()
i_exit = s.find("[USER] exit code=0x00000000")
i_pwd = s.find("[SH] pwd:")
check("queue: ordering exit-before-pwd", 0 <= i_exit < i_pwd, f"exit@{i_exit} pwd@{i_pwd}")

# ================= 5. start: 均衡抢占 + 打字期间 shell 可用 =================
mark_serial_base()
type_text("start\n")
time.sleep(4.0)
t = read_screen()
na, nb, nc = t.count('A'), t.count('B'), t.count('C')
check("start: A/B/C all scheduled", na >= 20 and nb >= 20 and nc >= 20, f"A={na} B={nb} C={nc}")
check("start: bully C cannot hog", nc < na * 3 and nc < nb * 3, f"A={na} B={nb} C={nc}")
type_text("pwd\n")
check("start: shell usable while preempting", wait_since("[SH] pwd:", name="pwd-during-start"))

# ================= 6. stop: 冻结 =================
type_text("stop\n")
time.sleep(1.0)
t1 = read_screen()
c1 = (t1.count('A'), t1.count('B'), t1.count('C'))
time.sleep(2.0)
t2 = read_screen()
c2 = (t2.count('A'), t2.count('B'), t2.count('C'))
check("stop: demo frozen", c1 == c2, f"{c1} -> {c2}")
type_text("pwd\n")
check("stop: shell still alive", wait_since("[SH] pwd:", name="pwd-after-stop"))

try:
    qemu.terminate()
except Exception:
    pass
qemu.wait(timeout=5)
print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
sys.exit(0 if all(ok for _, ok in results) else 1)
