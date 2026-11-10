#!/usr/bin/env python3
# Milestone 26b: normal boot enters Ring 3, recovery image enters kernel shell.
import os
import socket
import subprocess
import sys
import threading
import time

results = []
KEYMAP = {" ": "spc", "\n": "ret"}
NORMAL_IMG = os.environ.get("NEWOS_NORMAL_IMG", "build/os.img")
RECOVERY_IMG = os.environ.get("NEWOS_RECOVERY_IMG", "build/os-kernel.img")


def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}", flush=True)


class Guest:
    def __init__(self, image, suffix):
        self.mon = f"/tmp/newos-boot-{suffix}.sock"
        try:
            os.unlink(self.mon)
        except FileNotFoundError:
            pass
        self.serial = bytearray()
        self.qemu = subprocess.Popen(
            [
                "qemu-system-i386", "-fda", image, "-snapshot",
                "-display", "none",
                "-monitor", f"unix:{self.mon},server,nowait",
                "-serial", "stdio",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        threading.Thread(target=self._reader, daemon=True).start()
        for _ in range(200):
            if os.path.exists(self.mon):
                break
            time.sleep(0.05)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(self.mon)
        self.sock.settimeout(10)
        time.sleep(0.3)
        try:
            while True:
                data = self.sock.recv(65536)
                if not data or data.rstrip().endswith(b"(qemu)"):
                    break
        except socket.timeout:
            pass

    def _reader(self):
        while True:
            data = os.read(self.qemu.stdout.fileno(), 65536)
            if not data:
                break
            self.serial.extend(data)

    def log(self):
        return self.serial.decode(errors="replace")

    def send(self, command, wait=0.05):
        self.sock.sendall(command.encode() + b"\n")
        end = time.time() + 2
        while time.time() < end:
            try:
                data = self.sock.recv(65536)
                if not data or data.rstrip().endswith(b"(qemu)"):
                    break
            except socket.timeout:
                break
        time.sleep(wait)

    def type_text(self, text):
        for char in text:
            self.send(f"sendkey {KEYMAP.get(char, char)}", wait=0.08)

    def wait_for(self, needle, timeout, name):
        end = time.time() + timeout
        while time.time() < end:
            if needle in self.log():
                return True
            time.sleep(0.1)
        print(f"  [wait:{name}] tail={self.log()[-1400:]!r}", flush=True)
        return False

    def stop(self):
        try:
            self.qemu.terminate()
            self.qemu.wait(timeout=5)
        finally:
            self.sock.close()


normal = Guest(NORMAL_IMG, "normal")
try:
    check("default boot: splash", normal.wait_for("[SP] done", 30, "splash"))
    check("default boot: Ring 3 shell prompt",
          normal.wait_for("sh:/> ", 15, "user-prompt"))
    normal.type_text("pwd\n")
    check("default boot: user shell executes", normal.wait_for("/\n", 10, "pwd"))
    normal.type_text("exit\n")
    check("default boot: user shell exit",
          normal.wait_for("[USER] exit code=0x00000000", 15, "exit"))
    normal.type_text("pwd\n")
    check("default boot: recovery kernel shell",
          normal.wait_for("[SH] pwd: /", 12, "recovery"))
    normal.type_text("help\n")
    check("default boot: recovery help",
          normal.wait_for("[SH] help", 10, "recovery-help"))
    normal.type_text("cc\n")
    check("default boot: recovery cc dispatches",
          normal.wait_for("[SH] cc usage", 10, "recovery-cc"))
    normal.type_text("tetris\n")
    check("default boot: recovery game dispatches",
          normal.wait_for("[GAME] Mode 13h set, starting tetris", 10,
                          "recovery-game"))
    time.sleep(1.5)
    normal.send("sendkey spc", wait=0.4)
    time.sleep(0.8)
    normal.send("sendkey esc", wait=0.4)
    check("default boot: recovery game exits",
          normal.wait_for("[GAME] Tetris exited", 10, "recovery-game-exit"))
finally:
    normal.stop()

recovery = Guest(RECOVERY_IMG, "recovery")
try:
    check("recovery image: splash",
          recovery.wait_for("[SP] done", 30, "recovery-splash"))
    check("recovery image: no automatic user shell",
          "[PROC] spawn sh" not in recovery.log())
    recovery.type_text("pwd\n")
    check("recovery image: kernel shell direct",
          recovery.wait_for("[SH] pwd: /", 12, "kernel-prompt"))
finally:
    recovery.stop()

print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
sys.exit(0 if results and all(ok for _, ok in results) else 1)
