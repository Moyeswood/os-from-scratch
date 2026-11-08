#!/usr/bin/env python3
# 捕获里程碑 12 截图: run memdemo & → ps → QEMU screendump
import os
import socket
import subprocess
import threading
import time

IMG = "build/os-kernel.img"
MON = "/tmp/newos-memdemo-shot.sock"
SERIAL = "/tmp/newos-memdemo-shot.log"
PPM = "/tmp/newos-memdemo.ppm"
OUT = "note/memdemo.png"

for path in (MON, SERIAL, PPM):
    try:
        os.unlink(path)
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
    time.sleep(0.1)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MON)
sock.settimeout(10)


def send(cmd, wait=0.08):
    sock.settimeout(0.3)
    try:
        while sock.recv(65536):
            pass
    except socket.timeout:
        pass
    sock.settimeout(10)
    sock.sendall((cmd + "\n").encode())
    while True:
        try:
            data = sock.recv(65536)
            if not data or data.rstrip().endswith(b"(qemu)"):
                break
        except socket.timeout:
            break
    time.sleep(wait)


def type_text(text):
    for char in text:
        if char == " ":
            send("sendkey spc")
        elif char == "\n":
            send("sendkey ret")
        elif char == "&":
            send("sendkey shift-7")
        else:
            send(f"sendkey {char}", wait=0.12)


t0 = time.time()
while time.time() - t0 < 30:
    log = serial_buf.decode(errors="replace")
    if "[SP] done" in log or "[SP] skipped" in log:
        break
    time.sleep(0.2)
time.sleep(1.0)

type_text("run memdemo &\n")
t0 = time.time()
while time.time() - t0 < 12:
    log = serial_buf.decode(errors="replace")
    if "[MEM] parent-wait-start" in log and "[MEM] child-heap=7" in log:
        break
    time.sleep(0.2)
type_text("ps\n")
time.sleep(1.0)
send(f"screendump {PPM}", wait=1.0)

qemu.terminate()
qemu.wait(timeout=5)
serial_file.close()

subprocess.run(
    ["magick", PPM, "-crop", "720x396+0+0", "+repage", OUT],
    check=True)
print(f"captured {OUT}")
