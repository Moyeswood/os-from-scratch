#!/usr/bin/env python3
# Milestones 24-25: indirect blocks, concurrent FS, rename replacement,
# hard links, truncate-at-offset and reboot persistence.
import os
import socket
import subprocess
import sys
import threading
import time

OS_IMG = "build/os-kernel.img"
DISK_IMG = "build/test-fs2.img"

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
    def __init__(self, suffix):
        self.mon = f"/tmp/newos-fs2-{suffix}-mon.sock"
        self.serial_path = f"/tmp/newos-fs2-{suffix}-serial.log"
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
                "-drive", f"file={DISK_IMG},format=raw,if=ide,index=0",
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
        self.mark_pos = 0

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
        print(f"  [wait:{name}] serial tail={self.log()[-1600:]!r}", flush=True)
        return False

    def wait_ready(self):
        return self.wait_for(
            lambda: "[SP] done" in self.log() or "[SP] skipped" in self.log(),
            30.0,
            "boot",
        )

    def type_text(self, text):
        for char in text:
            key = "ret" if char == "\n" else KEYMAP.get(char, char)
            self.send(f"sendkey {key}", wait=0.08)

    def run(self, command, marker, timeout=60.0):
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
    first = Guest("first")
    try:
        check("fs2: boot completes", first.wait_ready())
        log = first.log()
        check("fs2: blank disk formatted",
              "[FS] no filesystem on disk, formatting" in log)
        check("fs2: 22 programs installed",
              "[EXEC] installed 0x00000016/0x00000016 programs in /bin" in log)

        first.mark()
        first.type_text("run fscheck\n")
        check("fs2: large-file and stress program",
              first.wait_for(lambda: "[FS2] done=1" in first.since() and
                             "(fscheck) exited code=0x00000000" in first.since(),
                             90.0, "fscheck"))
        text = first.since()
        check("indirect: 100KB write", "[FS2] big-write=1" in text)
        check("indirect: 100KB read-back", "[FS2] big-verify=1" in text)
        check("lock: concurrent child writes",
              "[FS2] concurrent-write=1" in text)
        check("lock: concurrent files verify",
              "[FS2] concurrent-verify=1" in text)
        check("rename: syscall and stat",
              "[FS2] rename-stat=1" in text)
        check("fscheck: exits cleanly",
              "(fscheck) exited code=0x00000000" in text)

        check("stat: 100KB indirect file",
              first.run("stat /fscheck/moved.bin",
                        "[SH] stat: type=0x00000001 size=0x00019000"))
        check("rename: kernel mv",
              first.run("mv /fscheck/moved.bin /fscheck/renamed.bin",
                        "[SH] mv ok"))
        check("rename: old path removed",
              first.run("stat /fscheck/moved.bin", "[SH] stat fail"))
        check("rename: new path visible",
              first.run("stat /fscheck/renamed.bin",
                        "[SH] stat: type=0x00000001 size=0x00019000"))

        check("rename: create replacement source",
              first.run("echo replacement-source > /fscheck/replace.src",
                        "[SH] echo ok"))
        check("rename: create replacement target",
              first.run("echo replacement-target > /fscheck/replace.dst",
                        "[SH] echo ok"))
        check("rename: overwrite existing file",
              first.run("mv /fscheck/replace.src /fscheck/replace.dst",
                        "[SH] mv ok"))
        check("rename: overwritten content wins",
              first.run("cat /fscheck/replace.dst",
                        "[SH] cat: replacement-source"))
        check("rename: overwritten source removed",
              first.run("stat /fscheck/replace.src", "[SH] stat fail"))

        first.run("echo hard-link-data > /fscheck/link.src", "[SH] echo ok")
        check("link: create hard link",
              first.run("ln /fscheck/link.src /fscheck/link.dst", "[SH] ln ok"))
        check("link: nlink becomes two",
              first.run("stat /fscheck/link.dst", " nlink=0x00000002"))
        check("link: content visible under both names",
              first.run("cat /fscheck/link.dst", "[SH] cat: hard-link-data"))
        first.run("rm /fscheck/link.src", "[SH] rm ok")
        check("link: data survives unlink",
              first.run("stat /fscheck/link.dst", " nlink=0x00000001"))
        first.run("rm /fscheck/link.dst", "[SH] rm ok")
        first.run("echo user-link-data > /fscheck/user-link.src", "[SH] echo ok")
        first.run("echo abcdefghijklmnop > /fscheck/truncate.src", "[SH] echo ok")
        check("truncate: shrink file",
              first.run("truncate /fscheck/truncate.src 5", "[SH] truncate ok"))
        check("truncate: shrink size",
              first.run("stat /fscheck/truncate.src",
                        "[SH] stat: type=0x00000001 size=0x00000005"))
        check("truncate: shrink content",
              first.run("cat /fscheck/truncate.src", "[SH] cat: abcde"))
        check("truncate: grow file",
              first.run("truncate /fscheck/truncate.src 10", "[SH] truncate ok"))
        check("truncate: grow size",
              first.run("stat /fscheck/truncate.src",
                        "[SH] stat: type=0x00000001 size=0x0000000A"))
        first.run("truncate /fscheck/truncate.src 3", "[SH] truncate ok")

        check("directory: create source",
              first.run("mkdir /fscheck/sub", "[SH] mkdir ok"))
        check("directory: cross-parent rename",
              first.run("mv /fscheck/sub /moved-dir", "[SH] mv ok"))
        check("directory: moved path resolves",
              first.run("cd /moved-dir", "[SH] cwd: /moved-dir"))
        check("directory: dotdot updated",
              first.run("cd ..", "[SH] cwd: /"))
        check("directory: cleanup",
              first.run("rm /moved-dir", "[SH] rm ok"))
        first.run("mkdir /empty-src", "[SH] mkdir ok")
        first.run("mkdir /empty-dst", "[SH] mkdir ok")
        check("rename: replace empty directory",
              first.run("mv /empty-src /empty-dst", "[SH] mv ok"))
        check("rename: replaced directory removed",
              first.run("stat /empty-src", "[SH] stat fail"))
        check("rename: target directory type",
              first.run("stat /empty-dst",
                        "[SH] stat: type=0x00000002"))
        first.run("mkdir /nonempty-src", "[SH] mkdir ok")
        first.run("mkdir /nonempty-dst", "[SH] mkdir ok")
        first.run("touch /nonempty-dst/child", "[SH] touch ok")
        check("rename: reject nonempty directory target",
              first.run("mv /nonempty-src /nonempty-dst", "[SH] mv fail"))
        first.run("rm /nonempty-dst/child", "[SH] rm ok")
        first.run("rm /nonempty-dst", "[SH] rm ok")
        first.run("rm /nonempty-src", "[SH] rm ok")
        first.run("rm /empty-dst", "[SH] rm ok")
        first.run("mkdir /tree", "[SH] mkdir ok")
        first.run("mkdir /tree/child", "[SH] mkdir ok")
        check("directory: reject move into subtree",
              first.run("mv /tree /tree/child/tree", "[SH] mv fail"))
        first.run("rm /tree/child", "[SH] rm ok")
        first.run("rm /tree", "[SH] rm ok")

        check("user shell: starts", first.run("run sh", "sh:/> "))
        check("user shell: stat utility",
              first.run("stat /fscheck/renamed.bin",
                        "type=file size=102400 nlink=1 inode="))
        first.mark()
        first.type_text("ls -l /fscheck\n")
        check("user shell: ls -l long format",
              first.wait_for(
                  lambda: " 16384 stress-0-0.bin" in first.since(),
                  15.0, "ls-long"))
        first.run("mv /fscheck/stress-0-0.bin /fscheck/user-moved.bin",
                  "sh:/> ")
        check("user shell: mv utility",
              first.run("stat /fscheck/user-moved.bin",
                        "type=file size=16384 nlink=1 inode="))
        first.run("ln /fscheck/user-link.src /fscheck/user-link.dst",
                  "sh:/> ")
        check("user shell: hard link utility",
              first.run("stat /fscheck/user-link.dst",
                        "type=file size=15 nlink=2 inode="))
        first.run("rm /fscheck/user-link.src", "sh:/> ")
        first.run("rm /fscheck/user-link.dst", "sh:/> ")
        first.run("truncate /fscheck/truncate.src 4", "sh:/> ")
        check("user shell: truncate utility",
              first.run("stat /fscheck/truncate.src",
                        "type=file size=4 nlink=1 inode="))
        check("user shell: exits", first.run("exit",
                                             "[USER] exit code=0x00000000"))
    finally:
        first.stop()

    with open(DISK_IMG, "rb") as image:
        image.seek(1024 * 1024)
        magic = image.read(4)
    check("disk: superblock retained", magic == b"NEOS", repr(magic))

    second = Guest("second")
    try:
        check("reboot: boot completes", second.wait_ready())
        check("reboot: filesystem mounted",
              "[FS] mounted disk at LBA 0x00000800" in second.log())
        check("reboot: filesystem validation",
              "[FS] selftest PASS" in second.log())
        second.mark()
        second.type_text("run fscheck verify\n")
        check("reboot: indirect file verifies",
              second.wait_for(
                  lambda: "[FS2] reboot-verify=1" in second.since() and
                  "(fscheck) exited code=0x00000000" in second.since(),
                  60.0, "fscheck-verify"))
        text = second.since()
        check("reboot: stat survives", "[FS2] reboot-stat=1" in text)
        check("reboot: renamed path survives",
              second.run("stat /fscheck/renamed.bin",
                        "[SH] stat: type=0x00000001 size=0x00019000"))
        check("reboot: old path stays absent",
              second.run("stat /fscheck/big.bin", "[SH] stat fail"))
        check("reboot: user-created directory survives",
              second.run("ls /fscheck", "[SH] ls: stress-1-2.bin"))
    finally:
        second.stop()

    print()
    print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
    return 0 if results and all(ok for _, ok in results) else 1


if __name__ == "__main__":
    sys.exit(main())
