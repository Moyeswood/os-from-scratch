#!/usr/bin/env python3
# 重新生成 note/ 截图 (渲染修复后), README 截图廊 + 字体记录用
import socket, subprocess, time, os, re, threading, sys
from PIL import Image

NEWOS = "/home/mfw/Documents/claude001/NewOS"
IMG = f"{NEWOS}/build/os.img"
MON = "/tmp/newos-repro/mon10.sock"
OUT = f"{NEWOS}/note"
try: os.unlink(MON)
except FileNotFoundError: pass

serial_buf = bytearray()
def reader(pipe):
    fd = pipe.fileno()
    while True:
        d = os.read(fd, 65536)
        if not d: break
        serial_buf.extend(d)

qemu = subprocess.Popen(
    ["qemu-system-i386", "-fda", IMG, "-snapshot", "-display", "vnc=127.0.0.1:9",
     "-monitor", f"unix:{MON},server,nowait", "-serial", "stdio"],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
threading.Thread(target=reader, args=(qemu.stdout,), daemon=True).start()
for _ in range(100):
    if os.path.exists(MON): break
    time.sleep(0.05)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MON); sock.settimeout(10)

def send(cmd, wait=0.15):
    sock.sendall((cmd + "\n").encode())
    out = b""
    end = time.time() + 3
    while time.time() < end:
        try:
            d = sock.recv(65536)
            if not d: break
            out += d
            if out.rstrip().endswith(b"(qemu)"): break
        except socket.timeout: break
    time.sleep(wait)
    return out.decode(errors="replace")

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
def type_text(s, wait=0.12):
    for ch in s:
        send(f"sendkey {KEYMAP.get(ch, ch)}", wait=wait)
def press(key, wait=0.3):
    send(f"sendkey {key}", wait=wait)

def wait_pat(pat, timeout=25):
    end = time.time() + timeout
    while time.time() < end:
        if re.search(pat.encode(), bytes(serial_buf)): return True
        time.sleep(0.1)
    return False

def shot(name):
    send(f"screendump /tmp/newos-repro/{name}.ppm", wait=0.6)
    im = Image.open(f"/tmp/newos-repro/{name}.ppm")
    im.save(f"{OUT}/{name}.png", optimize=True)
    print(f"captured {name}.png {im.size}")

# 1. 开机画面中途 (动画进行中, 不按键)
time.sleep(3.0)
shot("splash-mid")

# 2. 画面结束 → shell (banner + 提示)
if not wait_pat(r"\[SP\] (done|skipped)"): print("TIMEOUT [SP]"); sys.exit(1)
time.sleep(1.0)
shot("splash-shell")

# 3. font 18 help (默认字号)
type_text("help\n", wait=0.1)
shot("font18")

# 4. font 16 help
type_text("font 16\n", wait=0.1)
type_text("help\n", wait=0.1)
shot("font16")

# 5. font 12 提示符
type_text("font 12\n", wait=0.1)
shot("font12")

# 6. font 12 help
type_text("help\n", wait=0.1)
shot("font12-help")

# 7. font 14 help
type_text("font 14\n", wait=0.1)
type_text("help\n", wait=0.1)
shot("font14-help")

# 8. 文件系统操作 (font 18 默认)
type_text("font 18\n", wait=0.1)
type_text("mkdir demo\n", wait=0.1)
type_text("echo hello from NewOS > demo/a.txt\n", wait=0.1)
type_text("ls demo\n", wait=0.1)
type_text("cat demo/a.txt\n", wait=0.1)
type_text("df\n", wait=0.1)
shot("shell-fs")

# 9. 编辑器
type_text("edit demo/a.txt\n", wait=0.15)
wait_pat(r"\[ED\] (new|open):", timeout=10)
time.sleep(1.2)
type_text("Hello from the NewOS editor!", wait=0.08)
press("ret")
type_text("This file was typed at runtime.", wait=0.08)
press("ctrl-s", wait=0.4)
shot("editor")
press("ctrl-x", wait=0.5)
wait_pat(r"\[ED\] exit", timeout=8)

# 10. C 编译运行
type_text("edit /hello.c\n", wait=0.15)
wait_pat(r"\[ED\] (new|open):", timeout=10)
time.sleep(1.2)
type_text('int main() { printf("hello from C! (%d)\\n", 42); return 0; }', wait=0.08)
press("ctrl-s", wait=0.4)
press("ctrl-x", wait=0.5)
wait_pat(r"\[ED\] exit", timeout=8)
type_text("cc hello.c\n", wait=0.1)
wait_pat(r"hello from C! \(42\)", timeout=10)
time.sleep(0.5)
shot("cc-demo")

send("quit"); qemu.wait(timeout=10)
print("all done")
