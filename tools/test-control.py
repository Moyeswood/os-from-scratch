#!/usr/bin/env python3
# Milestone 34: Shell Control Flow Tests
# 按键经 QEMU monitor sendkey 注入 (真实键盘路径), 断言基于串口日志
# 相对 mark 的 since 片段, 避免历史文本误判; monitor socket 只发命令。
#
# 输出断言技巧: 每个用例先单独 mark 设变量, 再执行控制流命令 —
# 命令文本里只有 $VAR 没有字面值, 所以 since() 里出现字面值
# 只能来自真正的执行, 出现/不出现才是有效断言。
import os, socket, subprocess, sys, threading, time

IMG = "build/os-kernel.img"
MON = "/tmp/newos-cf-mon.sock"
SERIAL = "/tmp/newos-cf-serial.log"
for f in [MON, SERIAL]:
    try: os.unlink(f)
    except FileNotFoundError: pass

serial_file = open(SERIAL, "wb")
serial_buf = bytearray()

def serial_reader(pipe):
    while True:
        data = os.read(pipe.fileno(), 65536)
        if not data: break
        serial_buf.extend(data)
        serial_file.write(data)
        serial_file.flush()

qemu = subprocess.Popen(
    ["qemu-system-i386", "-fda", IMG, "-snapshot", "-display", "none",
     "-monitor", f"unix:{MON},server,nowait", "-serial", "stdio"],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
threading.Thread(target=serial_reader, args=(qemu.stdout,), daemon=True).start()

# Wait longer for boot + shell startup
time.sleep(10)

for _ in range(200):
    if os.path.exists(MON): break
    time.sleep(0.1)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MON)
sock.settimeout(10)

def send_raw(cmd, wait=0.03):
    sock.sendall((cmd + "\n").encode())
    time.sleep(wait)

def flush_monitor():
    try:
        while True:
            sock.settimeout(0.4)
            d = sock.recv(65536)
            if not d or d.rstrip().endswith(b"(qemu)"): break
    except socket.timeout:
        pass
    sock.settimeout(15)

KEYS = {
    " ": "spc", "/": "slash", ".": "dot",
    "-": "minus", "%": "shift-5",
    "<": "shift-comma", ">": "shift-dot",
    "|": "shift-backslash", "&": "shift-7",
    "'": "apostrophe", '"': "shift-apostrophe",
    "$": "shift-4", ";": "semicolon", "\\": "backslash",
    "?": "shift-slash", "*": "shift-8",
    "(": "shift-9", ")": "shift-0",
    "[": "bracket_left", "]": "bracket_right",
    "!": "shift-1", "=": "equal", ",": "comma",
}
for c in "abcdefghijklmnopqrstuvwxyz":
    KEYS[c] = c
for c in "0123456789":
    KEYS[c] = c
for c in range(ord("A"), ord("Z") + 1):
    KEYS[chr(c)] = f"shift-{chr(c + 32)}"

results = []
def check(name, cond):
    results.append((name, cond))
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {name}")
    return cond

base = 0
def mark():
    global base
    base = len(serial_buf)

def since():
    return serial_buf[base:].decode(errors="replace")

def type_text(text, wait=0.06):
    for ch in text:
        send_raw("sendkey ret" if ch == "\n" else f"sendkey {KEYS[ch]}", wait)

def run(cmd, wait=0.5):
    type_text(cmd)
    send_raw("sendkey ret")
    time.sleep(wait)
    flush_monitor()

# boot + skip splash, enter user shell
time.sleep(2.0)
flush_monitor()
for _ in range(4):
    send_raw("sendkey ret", 0.4)
    flush_monitor()
type_text("run sh")
send_raw("sendkey ret")
time.sleep(2.5)
flush_monitor()

print("=== Milestone 34: Shell Control Flow Tests ===")
check("shell up", "sh:/>" in serial_buf.decode(errors="replace"))

# ---- basic echo ----
mark()
run("echo hello-cf")
check("echo prints", "hello-cf" in since())

# ---- && ----
mark()
run("P=amp-l Q=amp-r")
mark()
run("echo $P && echo $Q")
check("&& runs both", "amp-l" in since() and "amp-r" in since())

mark()
run("P=no-amp")
mark()
run("false && echo $P")
check("&& short-circuits", "no-amp" not in since())

# ---- || ----
mark()
run("P=no-or")
mark()
run("true || echo $P")
check("|| skips on true", "no-or" not in since())

mark()
run("P=or-yes")
mark()
run("false || echo $P")
check("|| fires on false", "or-yes" in since())

# ---- ! negation ----
mark()
run("P=neg-no")
mark()
run("! true && echo $P")
check("! negates true", "neg-no" not in since())

mark()
run("P=neg-yes")
mark()
run("! false && echo $P")
check("! negates false", "neg-yes" in since())

# ---- grouped command list ----
mark()
run("P=g-one Q=g-two")
mark()
run("(echo $P; echo $Q)")
s = since()
check("grouped list", "g-one" in s and "g-two" in s)

# ---- if / elif / else ----
mark()
run("P=if-ok Q=if-no")
mark()
run("if true; then echo $P; else echo $Q; fi")
s = since()
check("if true branch", "if-ok" in s and "if-no" not in s)

mark()
run("if false; then echo $P; else echo $Q; fi")
s = since()
check("if else branch", "if-no" in s and "if-ok" not in s)

mark()
run("A=br-t B=br-e C=br-c")
mark()
run("if false; then echo $A; elif true; then echo $B; else echo $C; fi")
s = since()
check("elif branch", "br-e" in s and "br-t" not in s and "br-c" not in s)

# ---- while / break ----
mark()
run("W=w-never AB=after-w")
mark()
run("while false; do echo $W; done; echo $AB")
s = since()
check("while false skips body", "w-never" not in s and "after-w" in s)

mark()
run("AB2=brk-ok")
mark()
run("i=0; while true; do i=1; break; done; echo $AB2")
check("break exits while", "brk-ok" in since())

# ---- for over word list ----
mark()
run("for x in o1 o2 o3; do echo for-$x; done")
s = since()
check("for iterates words", "for-o1" in s and "for-o2" in s and "for-o3" in s)

# ---- while read with loop-level input redirect ----
mark()
run("echo L1 > /cfloop.txt; echo L2 >> /cfloop.txt")
mark()
run("RW=rw-end")
mark()
run("while read l; do echo got-$l; done < /cfloop.txt; echo $RW")
s = since()
check("while read consumes file", "got-L1" in s and "got-L2" in s)
check("stdin restored after loop", "rw-end" in s)
# 探针: 确认 /cfloop.txt 里确实有 2 行 (排空后仍可读)
mark()
run("cat /cfloop.txt")
s = since()
check("file has 2 lines after loop", "L1" in s and "L2" in s)

# ---- regression: shell still alive and responsive ----
mark()
run("AL=still-alive")
mark()
run("echo $AL")
check("shell alive at end", "still-alive" in since() and "killed by exception" not in since())

# Cleanup
try:
    qemu.terminate()
except:
    pass
try:
    qemu.wait(timeout=5)
except:
    pass
serial_file.close()

passed = sum(1 for _, ok in results if ok)
total = len(results)
print(f"\nSUMMARY: {total} checks, {passed} PASS, {total - passed} FAIL")
sys.exit(0 if passed == total else 1)
