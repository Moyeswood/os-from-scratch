#!/usr/bin/env python3
# Milestone 23: ATA PIO mount/read/write/write-back and reboot persistence.
import os
import socket
import subprocess
import sys
import threading
import time

OS_IMG = "build/os-kernel.img"
DISK_IMG = "build/test-disk.img"

KEYMAP = {
    " ": "spc",
    "/": "slash",
    ".": "dot",
    "-": "minus",
    ">": "shift-dot",
    "<": "shift-comma",
    "_": "shift-minus",
    "%": "shift-5",
}

results = []


def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}", flush=True)


class Guest:
    def __init__(self, suffix, disk):
        self.mon = f"/tmp/newos-disk-{suffix}-mon.sock"
        self.serial_path = f"/tmp/newos-disk-{suffix}-serial.log"
        for path in (self.mon, self.serial_path):
            try:
                os.unlink(path)
            except FileNotFoundError:
                pass

        self.serial_file = open(self.serial_path, "wb")
        self.serial_buf = bytearray()
        self.qemu = subprocess.Popen(
            [
                "qemu-system-i386",
                "-drive", f"file={OS_IMG},format=raw,if=floppy",
                "-drive", f"file={disk},format=raw,if=ide,index=0",
                "-display", "none",
                "-monitor", f"unix:{self.mon},server,nowait",
                "-serial", "stdio",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        threading.Thread(
            target=self._serial_reader, args=(self.qemu.stdout,), daemon=True
        ).start()

        for _ in range(200):
            if os.path.exists(self.mon):
                break
            time.sleep(0.05)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(self.mon)
        self.sock.settimeout(10)
        self._drain_greeting()

    def _serial_reader(self, pipe):
        while True:
            data = os.read(pipe.fileno(), 65536)
            if not data:
                break
            self.serial_buf.extend(data)
            self.serial_file.write(data)
            self.serial_file.flush()

    def _drain_greeting(self):
        time.sleep(0.3)
        try:
            while True:
                data = self.sock.recv(65536)
                if not data or data.rstrip().endswith(b"(qemu)"):
                    break
        except socket.timeout:
            pass

    def send(self, command, wait=0.05):
        self.sock.sendall(command.encode() + b"\n")
        data = b""
        deadline = time.time() + 2.0
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                data += chunk
                if data.rstrip().endswith(b"(qemu)"):
                    break
            except socket.timeout:
                break
        time.sleep(wait)
        return data.decode(errors="replace")

    def mark(self):
        self.mark_pos = len(self.serial_buf)

    def since(self):
        return self.serial_buf[self.mark_pos:].decode(errors="replace")

    def log(self):
        return self.serial_buf.decode(errors="replace")

    def wait_for(self, predicate, timeout, name):
        start = time.time()
        while time.time() - start < timeout:
            if predicate():
                return True
            time.sleep(0.15)
        print(f"  [wait:{name}] serial tail={self.log()[-1200:]!r}", flush=True)
        return False

    def wait_ready(self):
        return self.wait_for(
            lambda: "[SP] done" in self.log() or "[SP] skipped" in self.log(),
            25.0,
            "boot",
        )

    def type_text(self, text):
        for char in text:
            key = "ret" if char == "\n" else KEYMAP.get(char, char)
            self.send(f"sendkey {key}", wait=0.08)

    def run(self, command, marker, timeout=15.0):
        self.mark()
        self.type_text(command + "\n")
        return self.wait_for(lambda: marker in self.since(), timeout, command)

    def stop(self):
        try:
            self.qemu.terminate()
            self.qemu.wait(timeout=5)
        finally:
            self.sock.close()
            self.serial_file.close()


def fresh_disk():
    os.makedirs("build", exist_ok=True)
    with open(DISK_IMG, "wb") as image:
        image.truncate(2 * 1024 * 1024)


def main():
    fresh_disk()
    first = Guest("first", DISK_IMG)
    try:
        check("ata: primary master detected", first.wait_ready())
        log = first.log()
        check("ata: identify reports LBA28", "[ATA] primary master ready" in log)
        check("ata: blank disk is formatted",
              "[FS] no filesystem on disk, formatting" in log)
        check("fs: format persisted", "[FS] formatted disk:" in log)
        check("fs: initial selftest", "[FS] selftest PASS" in log)

        check("fs: create directory",
              first.run("mkdir /persist", "[SH] mkdir ok"), first.since()[-200:])
        check("fs: write file",
              first.run("echo durable data > /persist/note.txt",
                        "[SH] echo ok"), first.since()[-200:])
        check("fs: read file before reboot",
              first.run("cat /persist/note.txt",
                        "[SH] cat: durable data"), first.since()[-200:])
        check("fs: create removable entry",
              first.run("mkdir /persist/temp", "[SH] mkdir ok"),
              first.since()[-200:])
        check("fs: remove empty directory",
              first.run("rm /persist/temp", "[SH] rm ok"),
              first.since()[-200:])
    finally:
        first.stop()

    with open(DISK_IMG, "rb") as image:
        image.seek(1024 * 1024)
        magic = image.read(4)
    check("disk: superblock at LBA 2048", magic == b"NEOS", repr(magic))

    second = Guest("second", DISK_IMG)
    try:
        check("ata: reboot completes", second.wait_ready())
        log = second.log()
        check("fs: existing filesystem mounted",
              "[FS] mounted disk at LBA 0x00000800" in log)
        check("fs: no reformat after reboot",
              "[FS] no filesystem on disk" not in log)
        check("fs: mounted selftest", "[FS] selftest PASS" in log)
        check("bin: startup install is idempotent",
              "[EXEC] installed 0x00000017/0x00000017 programs in /bin" in log)
        check("fs: directory survives reboot",
              second.run("cd /persist", "[SH] cwd: /persist"),
              second.since()[-200:])
        check("fs: file content survives reboot",
              second.run("cat note.txt", "[SH] cat: durable data"),
              second.since()[-200:])
        check("fs: deletion survives reboot",
              second.run("cat temp", "[SH] cat fail"),
              second.since()[-200:])
    finally:
        second.stop()

    print()
    print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
    return 0 if results and all(ok for _, ok in results) else 1


if __name__ == "__main__":
    sys.exit(main())
