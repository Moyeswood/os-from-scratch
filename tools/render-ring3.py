#!/usr/bin/env python3
# 里程碑 10 截图: Ring 3 用户态 (run hello 输出 + crash 异常现场)
import socket, subprocess, time, os, re, threading, sys
from PIL import Image

NEWOS = "/home/mfw/Documents/claude001/NewOS"
IMG = f"{NEWOS}/build/os.img"
MON = "/tmp/newos-repro/mon-ring3.sock"
OUT = f"{NEWOS}/note"
os.makedirs("/tmp/newos-repro", exist_ok=True)
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

# 等开机画面结束
if not wait_pat(r"\[SP\] (done|skipped)"): print("TIMEOUT [SP]"); sys.exit(1)
time.sleep(1.0)

# 1. run hello: 用户态输出
type_text("run hello\n", wait=0.1)
wait_pat(r"\[USER\] exit code=0x00000000", timeout=10)
time.sleep(0.5)
shot("ring3-hello")

# 2. run crash: 页错误现场 (进程被杀, 回 shell)
type_text("run crash\n", wait=0.1)
wait_pat(r"\[USER\] process killed", timeout=10)
time.sleep(0.5)
shot("ring3-crash")

send("quit"); qemu.wait(timeout=10)
print("all done")
