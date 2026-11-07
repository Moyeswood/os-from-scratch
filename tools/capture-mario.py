#!/usr/bin/env python3
# 捕获马里奥标题画面和首个超级能力特效。
import os
import socket
import subprocess
import threading
import time

IMG = "build/os.img"
MON = "/tmp/newos-mario-shot.sock"
SERIAL = "/tmp/newos-mario-shot.log"

for path in (MON, SERIAL, "/tmp/mario-title.ppm", "/tmp/mario-game.ppm"):
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass

serial_buf = bytearray()


def serial_reader(pipe):
    while True:
        data = os.read(pipe.fileno(), 65536)
        if not data:
            break
        serial_buf.extend(data)


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


def send(cmd, wait=0.1):
    sock.sendall((cmd + "\n").encode())
    out = b""
    while True:
        try:
            data = sock.recv(65536)
            if not data:
                break
            out += data
            if out.rstrip().endswith(b"(qemu)"):
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
        else:
            send(f"sendkey {char}", 0.1)


deadline = time.time() + 25
while time.time() < deadline:
    log = serial_buf.decode(errors="replace")
    if "[SP] done" in log or "[SP] skipped" in log:
        break
    time.sleep(0.2)
time.sleep(1.0)

type_text("mario\n")
time.sleep(1.5)
send("screendump /tmp/mario-title.ppm", 1.0)

send("sendkey spc", 0.4)
time.sleep(0.8)
send("sendkey d 2000", 0.3)
send("sendkey spc", 0.3)
time.sleep(1.3)
send("sendkey p", 0.7)
send("screendump /tmp/mario-game.ppm", 1.0)

qemu.terminate()
qemu.wait(timeout=5)

subprocess.run(["magick", "/tmp/mario-title.ppm", "note/mario-title.png"],
               check=True)
subprocess.run(["magick", "/tmp/mario-game.ppm", "note/mario.png"], check=True)
print("captured note/mario-title.png and note/mario.png")
