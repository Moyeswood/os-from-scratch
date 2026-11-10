#!/usr/bin/env python3
# Built-in x86 assembler end-to-end test.
import os
import socket
import subprocess
import sys
import threading
import time

IMG = "build/os-kernel.img"
MON = "/tmp/newos-asm-mon.sock"
SERIAL = "/tmp/newos-asm-serial.log"
try:
    os.unlink(MON)
except FileNotFoundError:
    pass

serial_file = open(SERIAL, "wb")
serial_buf = bytearray()


def serial_reader(pipe):
    while True:
        data = os.read(pipe.fileno(), 65536)
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
    time.sleep(0.05)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MON)
sock.settimeout(10)
time.sleep(0.4)
try:
    while True:
        data = sock.recv(65536)
        if not data or data.rstrip().endswith(b"(qemu)"):
            break
except socket.timeout:
    pass

KEYS = {
    " ": "spc", "\n": "ret", "/": "slash", ".": "dot", ",": "comma",
    "-": "minus", "=": "equal", ">": "shift-dot", "<": "shift-comma",
    "_": "shift-minus", "+": "shift-equal", "!": "shift-1",
    '"': "shift-apostrophe", "'": "apostrophe", ";": "semicolon",
    ":": "shift-semicolon", "?": "shift-slash", "(": "shift-9",
    ")": "shift-0", "|": "shift-backslash", "\\": "backslash",
    "*": "shift-8", "&": "shift-7", "%": "shift-5", "^": "shift-6",
    "$": "shift-4", "#": "shift-3", "@": "shift-2",
}
KEYS.update({chr(c): f"shift-{chr(c + 32)}"
             for c in range(ord("A"), ord("Z") + 1)})


def send(command, wait=0.12):
    sock.sendall((command + "\n").encode())
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


def type_text(text, wait=0.17):
    for char in text:
        if char == "\n":
            send("sendkey ret", wait=wait)
        else:
            send(f"sendkey {KEYS.get(char, char)}", wait=wait)


def press(key, wait=0.3):
    send(f"sendkey {key}", wait=wait)


def log():
    return serial_buf.decode(errors="replace")


base = 0


def mark():
    global base
    base = len(serial_buf)


def since():
    return serial_buf[base:].decode(errors="replace")


results = []


def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}", flush=True)


def wait_since(needle, timeout=10.0, name=""):
    start = time.time()
    while time.time() - start < timeout:
        if needle in since():
            return True
        time.sleep(0.15)
    print(f"  [wait:{name}] tail={since()[-1000:]!r}", flush=True)
    return False


def write_editor_file(path, content):
    mark()
    type_text(f"edit {path}\n")
    if not wait_since(f"[ED] new: {path}", 10, "edit-new"):
        return False
    time.sleep(1.1)
    type_text(content)
    mark()
    press("ctrl-s")
    if not wait_since(f"[ED] saved: {path}", 8, "editor-save"):
        return False
    press("ctrl-x")
    return wait_since("[ED] exit: saved", 8, "editor-exit")


SOURCE = (
    "start:\n"
    "mov eax, 14\n"
    "call check\n"
    "jne bad\n"
    "mov ebx, 1\n"
    "mov ecx, message\n"
    "mov edx, 7\n"
    "int 0x80\n"
    "mov eax, 0\n"
    "xor ebx, ebx\n"
    "int 0x80\n"
    "bad:\n"
    "mov eax, 0\n"
    "mov ebx, 3\n"
    "int 0x80\n"
    "message:\n"
    "db \"ASM OK\", 10\n"
    "check:\n"
    "cmp eax, 14\n"
    "ret\n"
)

try:
    start = time.time()
    while time.time() - start < 20:
        if "[SP] done" in log() or "[SP] skipped" in log():
            break
        time.sleep(0.2)
    check("boot splash", "[SP] done" in log() or "[SP] skipped" in log())
    time.sleep(1.0)

    check("editor writes assembly source",
          write_editor_file("/hello.asm", SOURCE))

    mark()
    type_text("asm /hello.asm -o /bin/helloasm\n")
    check("assembler pass1", wait_since("[ASM] pass1 bytes=", 10, "asm-pass1"))
    check("assembler saves flat program",
          wait_since("[ASM] saved: /bin/helloasm", 10, "asm-save"))
    check("assembler flat output",
          wait_since("bytes=0x00000044", 5, "asm-size"))
    time.sleep(0.6)

    mark()
    type_text("run /bin/helloasm\n")
    check("assembled program starts",
          wait_since("[EXEC] file path=/bin/helloasm", 10, "asm-run"))
    check("assembled process spawned",
          wait_since("[PROC] spawn helloasm", 10, "asm-spawn"))
    check("assembled syscall output", wait_since("ASM OK\n", 10, "asm-output"))
    check("assembled program exits",
          wait_since("[USER] exit code=0x00000000", 10, "asm-exit"))

    mark()
    type_text("echo wat eax > /bad.asm\n")
    time.sleep(0.8)
    mark()
    type_text("asm /bad.asm -o /bin/bad\n")
    check("assembler reports unknown instruction",
          wait_since("msg=unknown instruction", 8, "asm-error"))
    check("assembler reports line number",
          wait_since("[ASM] error line=0x00000001", 5, "asm-line"))

    mark()
    type_text("debug /bin/helloasm\n")
    check("debug monitor loads image",
          wait_since("[DBG] loaded", 10, "dbg-load"))
    check("debug creates paused target",
          wait_since("[DBG] target paused", 10, "dbg-paused"))
    type_text("r\n")
    check("debug registers", wait_since("[DBG] r", 5, "dbg-r"))
    check("debug registers show real context",
          wait_since("[DBG] regs eip=0x02000000", 5, "dbg-real-r"))
    type_text("d 0x02000000\n")
    check("debug memory dump", wait_since("[DBG] d", 5, "dbg-d"))
    type_text("u 0x02000000\n")
    check("debug unassemble", wait_since("[DBG] u", 5, "dbg-u"))

    mark()
    type_text("t\n")
    check("debug single-step command", wait_since("[DBG] t", 5, "dbg-t"))
    check("debug TF trap returns to monitor",
          wait_since("[DBG] stop reason=single-step complete", 8, "dbg-t-stop"))
    check("debug single-step advances EIP",
          wait_since("[DBG] stop reason=single-step complete eip=0x02000005",
                     5, "dbg-t-eip"))

    mark()
    type_text("p\n")
    check("debug step-over command", wait_since("[DBG] p", 5, "dbg-p"))
    check("debug temporary return breakpoint hits",
          wait_since("[DBG] stop reason=step-over complete",
                     8, "dbg-p-stop"))
    check("debug step-over lands after call",
          wait_since("eip=0x0200000A", 5, "dbg-p-eip"))

    mark()
    type_text("bp 0x0200000A\n")
    check("debug installs breakpoint",
          wait_since("[DBG] bp set", 5, "dbg-bp-set"))
    type_text("bl\n")
    check("debug lists breakpoint",
          wait_since("[DBG] bp list", 5, "dbg-bp-list"))

    mark()
    type_text("g\n")
    check("debug breakpoint command", wait_since("[DBG] g", 5, "dbg-g"))
    check("debug INT3 stops at breakpoint",
          wait_since("[DBG] stop reason=breakpoint hit",
                     8, "dbg-bp-hit"))
    check("debug breakpoint reports address",
          wait_since("[DBG] stop reason=breakpoint hit eip=0x0200000A",
                     5, "dbg-bp-eip"))

    mark()
    type_text("g\n")
    check("debug continues from breakpoint", wait_since("[DBG] g", 5, "dbg-g2"))
    check("debug image output", wait_since("ASM OK\n", 10, "dbg-output"))
    check("debug returns from image",
          wait_since("[DBG] returned", 10, "dbg-return"))
    type_text("e 0x02000000 90\n")
    check("debug edits memory", wait_since("[DBG] e", 5, "dbg-e"))
    type_text("w\n")
    check("debug writes image", wait_since("[DBG] w", 5, "dbg-w"))
    mark()
    type_text("q\n")
    check("debug quits", wait_since("[DBG] exit", 5, "dbg-exit"))
finally:
    try:
        qemu.terminate()
    except Exception:
        pass
    qemu.wait(timeout=5)
    serial_file.close()

print()
print("SUMMARY:", f"{len(results)} checks",
      "ALL PASS" if results and all(ok for _, ok in results)
      else "SOME FAILED")
sys.exit(0 if results and all(ok for _, ok in results) else 1)
