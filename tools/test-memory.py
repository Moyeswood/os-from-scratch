#!/usr/bin/env python3
"""Milestone 12: E820 memory map, page reservations and kernel heap."""

import os
import re
import socket
import subprocess
import sys
import threading
import time

IMG = "build/os-kernel.img"
MON = "/tmp/newos-memory-mon.sock"
SERIAL = "/tmp/newos-memory-serial.log"

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


def monitor(command, wait=0.1):
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


def type_text(text, wait=0.06):
    for char in text:
        if char == " ":
            monitor("sendkey spc", wait)
        elif char == "\n":
            monitor("sendkey ret", wait)
        elif char == "\b":
            monitor("sendkey backspace", wait)
        else:
            monitor(f"sendkey {char}", wait)


def log():
    return serial_data.decode(errors="replace")


base = 0


def mark():
    global base
    base = len(serial_data)


def since():
    return log()[base:]


results = []


def check(name, condition, detail=""):
    results.append((name, bool(condition)))
    print(f"{name}: {'PASS' if condition else 'FAIL'} {detail}", flush=True)


def wait_for(condition, timeout=8.0, name=""):
    started = time.time()
    while time.time() - started < timeout:
        if condition():
            return True
        time.sleep(0.15)
    print(f"  timeout {name}: {log()[-240:]!r}", flush=True)
    return False


def wait_since(needle, timeout=8.0, name=""):
    return wait_for(lambda: needle in since(), timeout, name)


# Boot and built-in selftests.
check("E820 parser marker", wait_for(
    lambda: "[Memory] Parsing BIOS E820 map" in log(), 10, "e820-start"))
check("E820 map returned entries", wait_for(
    lambda: bool(re.search(r"\[MEM\] E820 entries=0x[0-9A-F]+ usable=0x[0-9A-F]+", log())),
    10, "e820-count"))
check("page allocator selftest", wait_for(
    lambda: "[MEM] selftest PASS" in log(), 10, "memory-selftest"))
check("kernel heap selftest", wait_for(
    lambda: "[HEAP] selftest PASS" in log(), 10, "heap-selftest"))
check("splash completed", wait_for(
    lambda: "[SP] done" in log() or "[SP] skipped" in log(),
    20, "splash"))
time.sleep(1.5)

mark()
type_text("meminfo\n")
check("meminfo command", wait_since("[SH] meminfo pages=", 10, "meminfo"))
check("meminfo reports E820", wait_since(" e820=0x", 10, "meminfo-e820"))

mark()
type_text("memmap\n")
check("memmap command", wait_since("[SH] memmap entries=", 10, "memmap"))
check("memmap lists several regions",
      wait_for(lambda: since().count("[MEM] map ") >= 3, 10, "memmap-entries"))
check("memmap exposes usable and reserved types",
      " type=0x00000001" in since() and " type=0x00000002" in since())

mark()
type_text("heap\n")
check("heap stats command", wait_since("[SH] heap: pages=", 10, "heap-stats"))
check("heap integrity is OK", wait_since(" check=OK", 10, "heap-check"))

mark()
type_text("heap blocks\n")
check("heap block list", wait_since("[SH] heap block", 10, "heap-blocks"))

mark()
type_text("heaptest\n")
check("heap stress test passes", wait_since("[HEAP] stress PASS", 20, "heap-stress"))

mark()
type_text("heap\n")
check("heap remains healthy after stress",
      wait_since(" check=OK", 10, "heap-after-stress"))

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
