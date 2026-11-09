#!/usr/bin/env python3
# Milestone 26: user signal action, pending/mask and sigreturn frames.
import os
import socket
import subprocess
import sys
import threading
import time

OS_IMG = "build/os-kernel.img"
MON = "/tmp/newos-signals-mon.sock"
KEYMAP = {" ": "spc", "\n": "ret"}
results = []


def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}", flush=True)


try:
    os.unlink(MON)
except FileNotFoundError:
    pass

serial = bytearray()
qemu = subprocess.Popen(
    [
        "qemu-system-i386",
        "-fda", OS_IMG,
        "-snapshot",
        "-display", "none",
        "-monitor", f"unix:{MON},server,nowait",
        "-serial", "stdio",
    ],
    stdout=subprocess.PIPE,
    stderr=subprocess.DEVNULL,
)


def reader():
    while True:
        data = os.read(qemu.stdout.fileno(), 65536)
        if not data:
            break
        serial.extend(data)


threading.Thread(target=reader, daemon=True).start()
for _ in range(200):
    if os.path.exists(MON):
        break
    time.sleep(0.05)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MON)
sock.settimeout(10)
time.sleep(0.3)
try:
    while True:
        data = sock.recv(65536)
        if not data or data.rstrip().endswith(b"(qemu)"):
            break
except socket.timeout:
    pass


def send(command, wait=0.05):
    sock.sendall(command.encode() + b"\n")
    deadline = time.time() + 2
    while time.time() < deadline:
        try:
            data = sock.recv(65536)
            if not data or data.rstrip().endswith(b"(qemu)"):
                break
        except socket.timeout:
            break
    time.sleep(wait)


def type_text(text):
    for char in text:
        send(f"sendkey {KEYMAP.get(char, char)}", wait=0.08)


def log():
    return serial.decode(errors="replace")


def wait_for(needle, timeout=12, name=""):
    end = time.time() + timeout
    while time.time() < end:
        if needle in log():
            return True
        time.sleep(0.1)
    print(f"  [wait:{name}] tail={log()[-1600:]!r}", flush=True)
    return False


try:
    check("signal: boot and splash", wait_for("[SP] done", 30, "boot"))
    type_text("run sigtest\n")
    markers = [
        "[SIGTEST] install-int=1",
        "[SIGTEST] handler-int=1",
        "[SIGTEST] self-signal=1",
        "[SIGTEST] install-term=1",
        "[SIGTEST] masked-pending=1",
        "[SIGTEST] handler-term=1",
        "[SIGTEST] unmask-deliver=1",
        "[SIGTEST] ignore=1",
        "[SIGTEST] returned=1",
        "[SIGTEST] install-restart=1",
        "[SIGTEST] restart-handler=1",
        "[SIGTEST] restart-resumed=1",
        "[SIGTEST] pipe-restart=1",
        "[SIGTEST] eintr=1",
        "[SIGTEST] done=1",
    ]
    for marker in markers:
        check(marker.removeprefix("[SIGTEST] "),
              wait_for(marker, 12, marker))
    check("signal: handler frames return cleanly",
          wait_for("(sigtest) exited code=0x00000000", 15, "exit"))
finally:
    try:
        qemu.terminate()
        qemu.wait(timeout=5)
    finally:
        sock.close()

print()
print("SUMMARY:", f"{len(results)} checks",
      "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
sys.exit(0 if results and all(ok for _, ok in results) else 1)
