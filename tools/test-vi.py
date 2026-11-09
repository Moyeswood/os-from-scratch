#!/usr/bin/env python3
# vi modal editor regression: modes, motions, yank/delete, search, commands.
import os
import socket
import subprocess
import sys
import threading
import time

IMG = "build/os-kernel.img"
MON = "/tmp/newos-vi-mon.sock"
SERIAL = "/tmp/newos-vi-serial.log"
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


def type_text(text, wait=0.16):
    for char in text:
        send(f"sendkey {KEYS.get(char, char)}", wait=wait)


def press(key, wait=0.25):
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
    print(f"  [wait:{name}] tail={since()[-800:]!r}", flush=True)
    return False


def run_command(command, needle=None, name=""):
    mark()
    type_text(command + "\n")
    ok = True
    if needle:
        ok = wait_since(needle, 12.0, name or command)
    time.sleep(0.5)
    return ok


def run_vi(path):
    mark()
    type_text(f"vi {path}\n")
    return wait_since("[VI] normal mode", 12.0, f"vi-{path}")


def colon(command):
    press("shift-semicolon", wait=0.2)
    type_text(command, wait=0.18)
    press("ret", wait=0.4)


try:
    start = time.time()
    while time.time() - start < 20:
        if "[SP] done" in log() or "[SP] skipped" in log():
            break
        time.sleep(0.2)
    check("boot splash", "[SP] done" in log() or "[SP] skipped" in log())
    time.sleep(1.0)

    check("vi opens", run_vi("/vi.txt"))
    mark()
    type_text("i")
    type_text("hello vi")
    press("esc")
    check("vi insert mode", wait_since("[VI] insert mode", 5, "insert"))
    check("vi escape to normal", wait_since("[VI] insert mode exit", 5,
                                            "normal"))

    press("home")
    press("l")
    press("a")
    type_text("X")
    press("esc")
    check("vi append after cursor", wait_since(
        "[VI] insert mode append", 5, "append"))

    press("o")
    type_text("second")
    press("esc")
    check("vi open line", wait_since("[VI] insert mode open line", 5,
                                     "open-line"))

    press("slash")
    type_text("second")
    press("ret")
    check("vi search", wait_since("[VI] search hit", 5, "search"))
    press("n")
    check("vi repeat search", wait_since("[VI] search hit", 5, "search-next"))

    mark()
    colon("w")
    check("vi :w saves", wait_since("[ED] saved: /vi.txt", 6, "vi-write"))
    mark()
    colon("q")
    check("vi :q exits clean buffer", wait_since("[VI] exit saved", 6,
                                                 "vi-quit"))
    time.sleep(0.8)
    check("vi content saved", run_command(
        "cat /vi.txt", "[SH] cat: heXllo vi", "vi-cat"))
    check("vi second line saved", "second" in log())

    check("vi reopens", run_vi("/vi.txt"))
    press("home")
    press("x")
    check("vi x deletes", wait_since("[VI] x", 5, "vi-x"))
    mark()
    press("u")
    check("vi u undoes", wait_since("[ED] undo", 5, "vi-undo"))
    mark()
    press("ctrl-r")
    check("vi ctrl-r redoes", wait_since("[ED] redo", 5, "vi-redo"))
    press("u")
    wait_since("[ED] undo", 5, "vi-undo-again")
    mark()
    press("y")
    press("y")
    check("vi yy yanks", wait_since("[VI] yy", 5, "vi-yy"))
    press("p")
    check("vi p pastes", wait_since("[VI] paste", 5, "vi-paste"))

    mark()
    colon("wq")
    check("vi :wq saves and exits", wait_since("[VI] exit saved", 6,
                                               "vi-wq"))
    time.sleep(0.8)

    check("vi reopens for dd", run_vi("/vi.txt"))
    press("home")
    press("d")
    press("d")
    check("vi dd deletes line", wait_since("[VI] dd", 5, "vi-dd"))
    colon("wq")
    check("vi dd save exit", wait_since("[VI] exit saved", 6, "vi-dd-exit"))
    time.sleep(0.8)
    check("vi dd applied", run_command(
        "cat /vi.txt", "[SH] cat: heXllo vi", "vi-dd-cat"))

    check("vi reopens for q guard", run_vi("/vi.txt"))
    type_text("i")
    type_text("X")
    press("esc")
    mark()
    colon("q")
    check("vi :q blocks modified buffer", wait_since("[VI] q blocked", 6,
                                                     "vi-q-block"))
    mark()
    colon("q!")
    check("vi :q! discards", wait_since("[VI] exit forced", 6, "vi-q-force"))
    check("vi discard logged", wait_since("[ED] exit: discarded", 6,
                                          "vi-discard"))
    time.sleep(0.8)
    check("vi discard preserves disk", run_command(
        "cat /vi.txt", "[SH] cat: heXllo vi", "vi-discard-cat"))
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
