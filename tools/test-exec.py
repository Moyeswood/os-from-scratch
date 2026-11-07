#!/usr/bin/env python3
"""Milestone 14: filesystem executable loading and transactional exec."""

import os
import re
import socket
import subprocess
import sys
import threading
import time

IMG = "build/os.img"
MON = "/tmp/newos-exec-mon.sock"
SERIAL = "/tmp/newos-exec-serial.log"

for path in (MON, SERIAL):
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass

serial_file = open(SERIAL, "wb")
serial_data = bytearray()


def serial_reader(pipe):
    while True:
        chunk = os.read(pipe.fileno(), 65536)
        if not chunk:
            break
        serial_data.extend(chunk)
        serial_file.write(chunk)
        serial_file.flush()


qemu = subprocess.Popen(
    [
        "qemu-system-i386",
        "-boot", "a",
        "-fda", IMG,
        "-snapshot",
        "-display", "none",
        "-monitor", f"unix:{MON},server,nowait",
        "-serial", "stdio",
    ],
    stdout=subprocess.PIPE,
    stderr=subprocess.DEVNULL,
)
threading.Thread(target=serial_reader, args=(qemu.stdout,), daemon=True).start()

for _ in range(100):
    if os.path.exists(MON):
        break
    time.sleep(0.1)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MON)
sock.settimeout(10)


def monitor(command, wait=0.08):
    sock.settimeout(0.3)
    try:
        while sock.recv(65536):
            pass
    except socket.timeout:
        pass
    sock.settimeout(10)
    sock.sendall((command + "\n").encode())
    output = b""
    while True:
        try:
            chunk = sock.recv(1 << 20)
            if not chunk:
                break
            output += chunk
            if output.rstrip().endswith(b"(qemu)"):
                break
        except socket.timeout:
            break
    time.sleep(wait)
    return output.decode(errors="replace")


def type_text(text, wait=0.09):
    for char in text:
        if char == " ":
            monitor("sendkey spc", wait)
        elif char == "\n":
            monitor("sendkey ret", wait)
        elif char == "/":
            monitor("sendkey slash", wait)
        elif char == "-":
            monitor("sendkey minus", wait)
        else:
            monitor(f"sendkey {char}", wait)


def log():
    return serial_data.decode(errors="replace")


base = 0


def mark():
    global base
    base = max(0, len(serial_data) - 64)


def since():
    return log()[base:]


results = []


def check(name, condition, detail=""):
    results.append((name, bool(condition)))
    print(f"{name}: {'PASS' if condition else 'FAIL'} {detail}", flush=True)


def wait_for(condition, timeout=10.0, name=""):
    started = time.time()
    while time.time() - started < timeout:
        if condition():
            return True
        time.sleep(0.15)
    print(f"  timeout {name}: {log()[-360:]!r}", flush=True)
    return False


def wait_since(needle, timeout=10.0, name=""):
    return wait_for(lambda: needle in since(), timeout, name)


check("boot: built-ins installed in /bin", wait_for(
    lambda: "[EXEC] installed 0x00000010/0x00000010 programs in /bin" in log(),
    15, "bin-install"))
check("boot: splash completed", wait_for(
    lambda: "[SP] done" in log() or "[SP] skipped" in log(), 20, "splash"))
time.sleep(1.0)

mark()
type_text("meminfo\n")
check("memory baseline command", wait_since("[SH] meminfo pages=", 10, "mem-before"))
match = re.search(r"\[SH\] meminfo pages=0x([0-9A-F]+) used=0x([0-9A-F]+) free=0x([0-9A-F]+)",
                  since())
used_before = int(match.group(2), 16) if match else -1

mark()
type_text("run /bin/hello\n")
check("file run: loader marker", wait_since("[EXEC] file path=/bin/hello", 10, "file-load"))
check("file run: process name from path", wait_since("[PROC] spawn hello", 10, "file-spawn"))
check("file run: program executes", wait_since("Hello from Ring 3!", 10, "file-output"))
check("file run: exits cleanly", wait_since("[USER] exit code=0x00000000", 10, "file-exit"))

mark()
type_text("run execdemo\n")
check("exec: demo starts", wait_since("[EXEC-DEMO] before pid=", 10, "exec-start"))
spawn = re.search(r"\[PROC\] spawn execdemo -> pid 0x([0-9A-F]+) \(pd=0x([0-9A-F]+)\)",
                  since())
check("exec: old pid and page directory captured", bool(spawn))
old_pid = spawn.group(1) if spawn else ""
old_pd = spawn.group(2) if spawn else ""
check("exec: file loaded from filesystem",
      wait_since("[EXEC] file path=/bin/hello", 10, "exec-file"))
check("exec: address space replaced",
      wait_since("[EXEC] replace pid=", 10, "exec-replace"))
replace = re.search(
    r"\[EXEC\] replace pid=0x([0-9A-F]+) old_pd=0x([0-9A-F]+) new_pd=0x([0-9A-F]+) path=/bin/hello",
    since())
check("exec: PID preserved and page directory swapped",
      bool(replace) and replace.group(1) == old_pid and
      replace.group(2) == old_pd and replace.group(3) != old_pd)
check("exec: new image starts at entry", wait_since("Hello from Ring 3!", 10, "exec-hello"))
check("exec: replaced process exits with same pid", wait_for(
    lambda: bool(old_pid) and
            f"[PROC] pid 0x{old_pid} (hello) exited code=0x00000000" in since(),
    10, "exec-exit"))

mark()
type_text("run execbad\n")
check("exec failure: missing file rejected",
      wait_since("[EXEC] invalid or missing executable: /bin/does-not-exist", 10, "exec-missing"))
check("exec failure: user receives -1", wait_since("[EXEC-BAD] missing-return=1", 10, "exec-ret"))
check("exec failure: kernel path rejected by syscall",
      wait_since("[SYS] exec: rejected path pointer", 10, "exec-badptr"))
check("exec failure: pointer rejection is visible to user",
      wait_since("[EXEC-BAD] badptr-return=1", 10, "exec-badptr-ret"))
check("exec failure: old image remains alive",
      wait_since("[EXEC-BAD] old-image-alive=1", 10, "exec-alive"))
check("exec failure: original process exits cleanly",
      wait_since("[USER] exit code=0x00000000", 10, "exec-bad-exit"))

mark()
type_text("run /bin/missing\n")
check("missing executable: spawn fails safely",
      wait_since("[USER] unknown program: /bin/missing", 10, "missing-spawn"))
type_text("pwd\n")
check("missing executable: shell remains usable", wait_since("[SH] pwd:", 10, "missing-pwd"))

mark()
type_text("run hello\n")
check("regression: embedded run still works", wait_since("Hello from Ring 3!", 10, "builtin"))
check("regression: embedded process exits cleanly",
      wait_since("[USER] exit code=0x00000000", 10, "builtin-exit"))

mark()
type_text("meminfo\n")
check("memory stats after exec cleanup", wait_since("[SH] meminfo pages=", 10, "mem-after"))
match = re.search(r"\[SH\] meminfo pages=0x([0-9A-F]+) used=0x([0-9A-F]+) free=0x([0-9A-F]+)",
                  since())
used_after = int(match.group(2), 16) if match else -2
check("memory: loader and exec release all transient pages",
      used_before == used_after, f"before={used_before} after={used_after}")

try:
    monitor("quit")
except Exception:
    pass
try:
    qemu.terminate()
except Exception:
    pass
qemu.wait(timeout=5)

print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
sys.exit(0 if all(ok for _, ok in results) else 1)
