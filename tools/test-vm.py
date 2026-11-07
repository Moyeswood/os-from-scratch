#!/usr/bin/env python3
"""Milestone 13: demand paging, COW, guard pages, brk/mmap."""

import os
import re
import socket
import subprocess
import sys
import threading
import time

IMG = "build/os.img"
MON = "/tmp/newos-vm-mon.sock"
SERIAL = "/tmp/newos-vm-serial.log"

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
    print(f"  timeout {name}: {log()[-320:]!r}", flush=True)
    return False


def wait_since(needle, timeout=10.0, name=""):
    return wait_for(lambda: needle in since(), timeout, name)


check("boot: per-process VM marker", wait_for(
    lambda: "[USER] per-process VM" in log(), 15, "vm-init"))
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
type_text("run vmtest\n")
check("vmtest: image mapped", wait_since("[VM] image pid=", 10, "vm-image"))
check("vmtest: brk starts at heap base", wait_since("[VM-TEST] brk-initial=1", 10, "brk-init"))
check("vmtest: heap pages demand fault", wait_since("[VM] demand", 10, "demand") and
      since().count("area=heap") >= 2)
check("vmtest: mmap pages demand fault", wait_for(
    lambda: since().count("area=mmap") >= 2, 10, "mmap-demand"))
check("vmtest: fork shares pages as COW", wait_since("[VM] fork pid=", 10, "cow-fork") and
      "shared_pages=" in since() and "(COW)" in since())
check("vmtest: writes copy shared pages", wait_for(
    lambda: since().count("[VM] COW pid=") >= 2, 15, "cow-handled"))
check("vmtest: child sees private values", wait_since("[VM-TEST] child-heap=99", 15, "child-heap") and
      "[VM-TEST] child-mmap=88" in since())
check("vmtest: parent is isolated", wait_for(
    lambda: "[VM-TEST] parent-inherited=5" in since() and
            "[VM-TEST] parent-heap=11" in since() and
            "[VM-TEST] parent-mmap=33" in since(), 15, "parent-values"))
check("vmtest: brk shrink succeeds", wait_since("[VM-TEST] brk-shrink=1", 15, "brk-shrink"))
check("vmtest: program reports success", wait_since("[VM-TEST] done=1", 15, "vm-done"))
check("vmtest: exit code zero", wait_since("[USER] exit code=0x00000000", 15, "vm-exit"))

mark()
type_text("run vmguard\n")
check("guard: write reaches guard page", wait_since("[VM-GUARD] writing", 10, "guard-start"))
check("guard: VM rejects the address", wait_since("stack guard page", 10, "guard-reject"))
check("guard: process gets a page fault", wait_since("[EXC] #14 Page Fault", 10, "guard-pf"))
check("guard: process is killed", wait_since("killed by exception", 10, "guard-kill"))

mark()
type_text("run hello\n")
check("guard: shell remains usable", wait_since("Hello from Ring 3!", 15, "hello-after-guard"))
check("guard: zombie child was reclaimed",
      "[PROC] swept orphan zombie" in log())

mark()
type_text("meminfo\n")
check("memory stats after cleanup", wait_since("[SH] meminfo pages=", 10, "mem-after"))
match = re.search(r"\[SH\] meminfo pages=0x([0-9A-F]+) used=0x([0-9A-F]+) free=0x([0-9A-F]+)",
                  since())
used_after = int(match.group(2), 16) if match else -2
check("memory: per-process pages all reclaimed", used_before == used_after,
      f"before={used_before} after={used_after}")

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
