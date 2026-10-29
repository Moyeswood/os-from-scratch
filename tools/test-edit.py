#!/usr/bin/env python3
# 内置编辑器测试 (串口驱动): 打开/新建/插入/导航/删除/保存/修改警告/错误路径
# 复用 test-fs.py 基建: monitor socket + 管道串口 + 基线机制 + 键名映射。
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
# ★大写字母: QEMU 键名没有大写, 必须发 shift-<小写> (直接发 "X" 会被
# 静默丢弃 — 实测警告测试无修改、行号 L 全丢)
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

# 进入编辑器: edit <path>, 等打开标记, 静置 (打开后 kbd_drain 会吃掉早到的按键)
def run_edit(path, timeout=8.0):
    mark_serial_base()
    type_text(f"edit {path}\n")
    ok = wait_for(lambda: f"[ED] new: {path}" in serial_since()
                        or f"[ED] open: {path}" in serial_since()
                        or "[ED] fail" in serial_since(),
                  timeout=timeout, name=f"edit-{path}")
    time.sleep(1.2)   # ★kbd_drain 窗口: 早到的按键会被排空吃掉
    return ok

# 保存并退出
def save_exit():
    mark_serial_base()
    press("ctrl-s", wait=0.4)
    ok1 = wait_for(lambda: "[ED] saved:" in serial_since(), timeout=6.0, name="ctrl-s")
    press("ctrl-x", wait=0.4)
    ok2 = wait_for(lambda: "[ED] exit: saved" in serial_since(), timeout=6.0, name="ctrl-x")
    time.sleep(0.8)
    return ok1 and ok2

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

# ---------- 2. 新建 + 输入 + 保存 ----------
check("edit new file", run_edit("/t.txt"))
type_text("hello editor")
mark_serial_base()
press("ctrl-s", wait=0.4)
check("ctrl-s saves 12 bytes", wait_for(
    lambda: "[ED] saved: /t.txt (0x0000000C bytes)" in serial_since(), name="save12"))
press("ctrl-x", wait=0.4)
check("exit saved", wait_for(lambda: "[ED] exit: saved" in serial_since(), name="exit1"))
time.sleep(0.6)
check("cat verifies content", run_cmd("cat /t.txt", "[SH] cat: hello editor", name="cat-t"))

# ---------- 3. 重新打开 (open 路径) + End/!/Home 导航 ----------
check("edit existing file", run_edit("/t.txt"))
press("end", wait=0.4)
type_text("!")
mark_serial_base()
press("ctrl-s", wait=0.4)
check("append at end", wait_for(
    lambda: "[ED] saved: /t.txt (0x0000000D bytes)" in serial_since(), name="save13"))
press("ctrl-x", wait=0.4)
wait_for(lambda: "[ED] exit: saved" in serial_since(), name="exit2")
time.sleep(0.6)
check("cat shows appended", run_cmd("cat /t.txt", "[SH] cat: hello editor!", name="cat-bang"))

# ---------- 4. 两行编辑 + 上下导航 ----------
check("edit two-line file", run_edit("/m.txt"))
type_text("alpha\nbeta")
press("up", wait=0.4)          # 到第 1 行
press("end", wait=0.4)         # 行尾
type_text("1")                 # alpha1
press("down", wait=0.4)
press("end", wait=0.4)
type_text("2")                 # beta2
check("save two-line", save_exit())
time.sleep(0.6)
check("cat alpha1", run_cmd("cat /m.txt", "[SH] cat: alpha1", name="cat-alpha"))
check("cat beta2", "beta2" in serial_log())

# ---------- 5. Delete 键 ----------
check("edit delete file", run_edit("/d.txt"))
type_text("abc")
press("home", wait=0.4)
press("delete", wait=0.4)      # 删 'a'
check("save delete", save_exit())
time.sleep(0.6)
check("cat bc", run_cmd("cat /d.txt", "[SH] cat: bc", name="cat-bc"))

# ---------- 6. backspace ----------
check("edit bs file", run_edit("/e.txt"))
type_text("xy")
press("home", wait=0.4)
press("right", wait=0.4)
press("backspace", wait=0.4)   # 删 'x'
check("save bs", save_exit())
time.sleep(0.6)
check("cat y", run_cmd("cat /e.txt", "[SH] cat: y", name="cat-y"))

# ---------- 7. Tab 展开 ----------
check("edit tab file", run_edit("/tab.txt"))
press("tab", wait=0.4)
type_text("a")
check("save tab", save_exit())
time.sleep(0.6)
check("cat tab", run_cmd("cat /tab.txt", "[SH] cat:     a", name="cat-tab"))

# ---------- 8. 修改警告: ^X → 警告 → ^X → 丢弃 ----------
check("edit for warn", run_edit("/t.txt"))
type_text("X")
mark_serial_base()
press("ctrl-x", wait=0.4)
check("first ^X warns", wait_for(lambda: "[ED] modified warning" in serial_since(), name="warn"))
press("ctrl-x", wait=0.4)
check("second ^X discards", wait_for(lambda: "[ED] exit: discarded" in serial_since(), name="discard"))
time.sleep(0.6)
check("content unchanged", run_cmd("cat /t.txt", "[SH] cat: hello editor!", name="cat-unchanged"))

# ---------- 9. PgUp 翻页 ----------
check("edit page file", run_edit("/p.txt"))
for i in range(1, 31):
    type_text(f"L{i:02d}\n", wait=0.15)
press("pgup", wait=0.4)
press("pgup", wait=0.4)        # 光标夹到第 1 行
type_text("X")                 # 行首插入 X
check("save page", save_exit())
time.sleep(0.6)
check("cat X at top", run_cmd("cat /p.txt", "[SH] cat: XL01", name="cat-pg"))

# ---------- 10. 错误路径 ----------
check("edit dir → error", run_edit("/"))
check("dir error marker", "[ED] fail: is a directory" in serial_since())
type_text("\n")                # 编辑器失败直接返回 shell, 这条命令是空回车
time.sleep(0.6)
check("edit missing parent → error", run_edit("/nope/x.txt"))
check("create error marker", "[ED] fail: cannot create" in serial_since())
type_text("\n")
time.sleep(0.6)
check("shell still alive", run_cmd("pwd", "[SH] pwd: /", name="pwd-alive"))

# ---------- 11. 状态栏渲染抽查 (偶数字节 = 字符, 奇数字节 = 属性) ----------
mark_serial_base()
type_text("edit /t.txt\n")
wait_for(lambda: "[ED] open: /t.txt" in serial_since(), name="reopen")
time.sleep(1.5)
ok_bar = False
for _ in range(4):
    time.sleep(1.5)
    r = send("xp /80bx 0xb8e60", wait=0.05)   # 第 23 行 (状态栏, 25 行屏幕)
    chars = ""
    for line in r.splitlines():
        if ':' not in line or '\x1b' in line: continue
        toks = line.split(':', 1)[1].split()
        for i, tok in enumerate(toks):
            if tok.startswith('0x') and i % 2 == 0:
                c = int(tok, 16)
                if 32 <= c < 127: chars += chr(c)
    if "Save" in chars and "Exit" in chars:
        ok_bar = True
        break
check("screen: status bar", ok_bar, "editor still open")
press("ctrl-x", wait=0.4)    # 未修改 → 一次 ^X 直接退出 (多按会向 shell 输入 'x')
type_text("\n")              # 冲掉可能的残留输入
time.sleep(0.8)

# ---------- 12. 回归 ----------
check("regression: fs mkdir", run_cmd("mkdir ed-regress", "[SH] mkdir ok", name="reg-mkdir"))
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
