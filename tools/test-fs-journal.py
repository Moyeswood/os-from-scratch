#!/usr/bin/env python3
# Milestone 25: redo-journal replay after a simulated power loss.
import os
import socket
import subprocess
import sys
import threading
import time

OS_IMG = "build/os-kernel.img"
DISK_IMG = "build/test-journal.img"

KEYMAP = {
    " ": "spc",
    "/": "slash",
    ".": "dot",
    "-": "minus",
    "_": "shift-minus",
}

results = []


def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}", flush=True)


class Guest:
    def __init__(self, suffix):
        self.mon = f"/tmp/newos-journal-{suffix}-mon.sock"
        for path in (self.mon,):
            try:
                os.unlink(path)
            except FileNotFoundError:
                pass
        self.serial = bytearray()
        self.qemu = subprocess.Popen(
            [
                "qemu-system-i386",
                "-drive", f"file={OS_IMG},format=raw,if=floppy",
                "-drive", f"file={DISK_IMG},format=raw,if=ide,index=0",
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
            key = "ret" if char == "\n" else KEYMAP.get(char, char)
            self.send(f"sendkey {key}", wait=0.08)

    def wait_for(self, needle, timeout, name):
        end = time.time() + timeout
        while time.time() < end:
            if needle in self.log():
                return True
            time.sleep(0.1)
        print(f"  [wait:{name}] tail={self.log()[-1600:]!r}", flush=True)
        return False

    def wait_ready(self):
        return self.wait_for("[SP] done", 30, "boot")

    def stop(self):
        try:
            self.qemu.terminate()
            self.qemu.wait(timeout=5)
        finally:
            self.sock.close()


def fresh_disk():
    os.makedirs("build", exist_ok=True)
    with open(DISK_IMG, "wb") as image:
        image.truncate(2 * 1024 * 1024)


def main():
    fresh_disk()
    first = Guest("power-loss")
    try:
        check("journal: initial boot", first.wait_ready())
        first.type_text("fscrash\n")
        check("journal: crashpoint committed",
              first.wait_for("[FS] crashpoint armed", 15, "crashpoint"))
        check("journal: crashpoint has metadata",
              "meta=0x00000003" in first.log())
    finally:
        first.stop()              # Hard QEMU termination = simulated power loss.

    second = Guest("recover")
    try:
        check("recovery: reboot completes", second.wait_ready())
        text = second.log()
        check("recovery: committed journal detected",
              "[FS] replaying committed journal" in text)
        check("recovery: journal replay complete",
              "[FS] journal replay complete" in text)
        check("recovery: filesystem validates",
              "[FS] selftest PASS" in text)

        second.type_text("cat /__journal-crash.txt\n")
        check("recovery: metadata and data restored",
              second.wait_for("[SH] cat: journal replay restored this file",
                              12, "cat-recovered"))
        second.type_text("sync\n")
        check("recovery: explicit sync",
              second.wait_for("[SH] sync ok", 12, "sync"))
    finally:
        second.stop()

    print()
    print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
    return 0 if results and all(ok for _, ok in results) else 1


if __name__ == "__main__":
    sys.exit(main())
