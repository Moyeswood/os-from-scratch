#!/usr/bin/env python3
# 里程碑 12 测试: 用户堆 (sbrk/malloc) + fork 堆隔离 + waitpid
#   - sbrk 返回旧断点并清零新增区域; 超大 sbrk 安全失败
#   - fork 后的堆修改不串回父进程
#   - 活父进程的 ZOMBIE 不会被下一次 spawn 的孤儿清扫误收
#   - waitpid(-1, &status) 返回正确 pid/退出码并释放孩子槽位
import os
import socket
import subprocess
import sys
import threading
import time

IMG = "build/os-kernel.img"
MON = "/tmp/newos-memproc-mon.sock"
SERIAL = "/tmp/newos-memproc-serial.log"
try:
    os.unlink(MON)
except FileNotFoundError:
    pass

serial_file = open(SERIAL, "wb")
serial_buf = bytearray()


def serial_reader(pipe):
    fd = pipe.fileno()
    while True:
        data = os.read(fd, 65536)
        if not data:
            break
        serial_buf.extend(data)
        serial_file.write(data)
        serial_file.flush()


qemu = subprocess.Popen(
    ["qemu-system-i386", "-fda", IMG, "-snapshot", "-display", "none",
     "-monitor", f"unix:{MON},server,nowait", "-serial", "stdio"],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
threading.Thread(target=serial_reader, args=(qemu.stdout,), daemon=True).start()

for _ in range(100):
    if os.path.exists(MON):
        break
    time.sleep(0.1)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MON)
sock.settimeout(10)


def send(cmd, wait=0.1):
    sock.settimeout(0.3)
    try:
        while True:
            data = sock.recv(65536)
            if not data:
                break
    except socket.timeout:
        pass
    sock.settimeout(10)
    sock.sendall((cmd + "\n").encode())
    out = b""
    while True:
        try:
            data = sock.recv(1048576)
            if not data:
                break
            out += data
            if out.rstrip().endswith(b"(qemu)"):
                break
        except socket.timeout:
            break
    time.sleep(wait)
    return out.decode(errors="replace")


def type_text(text, wait=0.1):
    for ch in text:
        if ch == " ":
            send("sendkey spc", wait=wait)
        elif ch == "\n":
            send("sendkey ret", wait=wait)
        elif ch == "&":
            send("sendkey shift-7", wait=wait)
        else:
            send(f"sendkey {ch}", wait=wait)


def serial_log():
    return serial_buf.decode(errors="replace")


serial_base = 0


def mark_serial_base():
    global serial_base
    serial_base = max(0, len(serial_buf) - 64)


def serial_since():
    return serial_log()[serial_base:]


def read_screen(nwords=1600):
    response = send(f"xp /{nwords}wx 0xb8000", wait=0.05)
    text = ""
    for line in response.splitlines():
        if ":" not in line or "\x1b" in line:
            continue
        for token in line.split(":", 1)[1].split():
            if token.startswith("0x"):
                value = int(token, 16)
                for shift in (0, 8, 16, 24):
                    char = (value >> shift) & 0xFF
                    if 32 <= char < 127:
                        text += chr(char)
    return text


results = []


def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}", flush=True)


def wait_for(cond, timeout=10.0, name=""):
    start = time.time()
    while time.time() - start < timeout:
        if cond():
            return True
        time.sleep(0.2)
    if cond():
        return True
    print(f"  [wait_for:{name}] timeout: tail={serial_log()[-300:]!r}", flush=True)
    return False


def wait_since(needle, timeout=10.0, name=""):
    return wait_for(lambda: needle in serial_since(), timeout, name)


# ================= 启动 =================
time.sleep(3.0)
check("boot: scheduler marker", "[TASK] scheduler" in serial_log())
check("boot: splash done", wait_for(
    lambda: "[SP] done" in serial_log() or "[SP] skipped" in serial_log(),
    timeout=15.0, name="splash-done"))
time.sleep(0.8)

# ================= 用户堆 =================
mark_serial_base()
type_text("run memdemo &\n")
check("heap: background spawn", wait_since("[PROC] spawn memdemo", name="spawn"))
check("heap: initial break is 0x2040000", wait_since("[MEM] break-start=33816576", name="brk"))
check("heap: sbrk returns old break", wait_since("[MEM] sbrk-old=33816576", name="sbrk-old"))
check("heap: extended bytes are zero", wait_since("[MEM] sbrk-zero=1", name="sbrk-zero"))
check("heap: malloc succeeds", wait_since("[MEM] malloc-ok=1", name="malloc"))
check("heap: malloc memory starts zero", wait_since("[MEM] malloc-zero=1", name="malloc-zero"))
check("heap: parent initial value", wait_since("[MEM] heap-parent-before=1", name="heap-before"))

# ================= fork 堆隔离 =================
check("fork: child reports pid", wait_since("[MEM] child-pid=", name="child-pid"))
check("fork: child changes own heap", wait_since("[MEM] child-heap=7", name="child-heap"))
check("fork: child exits 42", wait_since("exited code=0x0000002A", name="child-exit"))
check("fork: parent starts delayed wait", wait_since("[MEM] parent-wait-start", name="wait-start"))

# 孩子已是 ZOMBIE, 但父进程还活着且在 sleep；ps 应看到它。
type_text("ps\n")
time.sleep(0.5)
screen = read_screen()
check("zombie: live parent's child remains visible", "ZOMBIE" in screen and "memdemo" in screen)

# 这次 spawn 会运行 sweep_zombies。活父进程的孩子不能被误收。
mark_serial_base()
type_text("run hello &\n")
check("zombie: unrelated spawn succeeds", wait_since("[PROC] spawn hello", name="hello-bg"))
check("zombie: live parent's child not swept", "[PROC] swept orphan zombie" not in serial_since())

# ================= waitpid =================
check("waitpid: returns child pid", wait_for(
    lambda: "[MEM] waitpid-pid=" in serial_since() and
    "waitpid-pid=4294967295" not in serial_since(), 20.0, "wait-pid"))
check("waitpid: reports exit status 42", wait_since("[MEM] waitpid-status=42", name="wait-status"))
check("fork: parent heap remains 1", wait_since("[MEM] heap-parent-after=1", name="heap-after"))
check("waitpid: no-child returns -1", wait_since("[MEM] waitpid-none=4294967295", name="wait-none"))
check("waitpid: rejects bad status pointer",
      wait_since("[SYS] waitpid: rejected status pointer", name="wait-badptr"))
check("sbrk: huge request fails safely", wait_since("[MEM] sbrk-overflow=1", name="sbrk-overflow"))
check("memdemo: all in-process checks pass", wait_since("[MEM] done=1", name="mem-done"))
check("memdemo: exits zero",
      wait_since("(memdemo) exited code=0x00000000", name="mem-exit"))

# ================= 孤儿回收与槽位复用 =================
mark_serial_base()
type_text("run hello\n")
check("reuse: foreground hello still runs", wait_since("Hello from Ring 3!", name="reuse-hello"))
check("reuse: detached zombies eventually swept",
      wait_since("[PROC] swept orphan zombie", name="swept"))

try:
    qemu.terminate()
except Exception:
    pass
qemu.wait(timeout=5)
print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
sys.exit(0 if all(ok for _, ok in results) else 1)
