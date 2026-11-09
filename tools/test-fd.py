#!/usr/bin/env python3
# 里程碑 19: 进程级文件描述符与工作目录
import os
import socket
import subprocess
import sys
import threading
import time

IMG = "build/os-kernel.img"
MON = "/tmp/newos-fd-mon.sock"
SERIAL = "/tmp/newos-fd-serial.log"
try:
    os.unlink(MON)
except FileNotFoundError:
    pass

serial_file = open(SERIAL, "wb")
serial_buf = bytearray()


def serial_reader(pipe):
    fd = pipe.fileno()
    while True:
        data = os.read(fd, 65536)
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


def send(cmd, wait=0.1):
    sock.settimeout(0.3)
    try:
        while sock.recv(65536):
            pass
    except socket.timeout:
        pass
    sock.settimeout(10)
    sock.sendall((cmd + "\n").encode())
    out = b""
    while True:
        try:
            data = sock.recv(1048576)
            if not data:
                break
            out += data
            if out.rstrip().endswith(b"(qemu)"):
                break
        except socket.timeout:
            break
    time.sleep(wait)
    return out.decode(errors="replace")


def type_text(text, wait=0.12):
    for ch in text:
        if ch == " ":
            send("sendkey spc", wait=wait)
        elif ch == "\n":
            send("sendkey ret", wait=wait)
        elif ch == "/":
            send("sendkey slash", wait=wait)
        elif ch == ".":
            send("sendkey dot", wait=wait)
        else:
            send(f"sendkey {ch}", wait=wait)


def log():
    return serial_buf.decode(errors="replace")


base = 0


def mark():
    global base
    base = max(0, len(serial_buf) - 64)


def since():
    return log()[base:]


results = []


def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}", flush=True)


def wait_for(cond, timeout=10.0, name=""):
    start = time.time()
    while time.time() - start < timeout:
        if cond():
            return True
        time.sleep(0.2)
    if cond():
        return True
    print(f"  [wait:{name}] timeout tail={log()[-500:]!r}", flush=True)
    return False


def wait_since(needle, timeout=10.0, name=""):
    return wait_for(lambda: needle in since(), timeout, name)


try:
    time.sleep(3.0)
    check("boot: scheduler marker", "[TASK] scheduler" in log())
    check("boot: splash done", wait_for(
        lambda: "[SP] done" in log() or "[SP] skipped" in log(),
        20.0, "splash"))
    time.sleep(1.0)

    mark()
    type_text("run filedemo\n")
    expected = [
        ("pid spawn", "[PROC] spawn filedemo"),
        ("chdir succeeds", "[FD] chdir=0"),
        ("getcwd succeeds", "[FD] getcwd=1"),
        ("cwd value", "[FD] cwd=/fdhome"),
        ("open create", "[FD] open-create=1"),
        ("write first", "[FD] write-first=1"),
        ("lseek end", "[FD] lseek-end=1"),
        ("write second", "[FD] write-second=1"),
        ("lseek start", "[FD] lseek-start=1"),
        ("read first", "[FD] read-first=1"),
        ("read content", "[FD] content-first=1"),
        ("read bad pointer", "[FD] read-badptr=1"),
        ("close", "[FD] close=1"),
        ("close bad fd", "[FD] close-badfd=1"),
        ("reopen read-only", "[FD] open-read=1"),
        ("read whole file", "[FD] read-all=1"),
        ("file content", "[FD] content=kernel fd"),
        ("child cwd isolated", "[FD] child-cwd-isolated=1"),
        ("child reaped", "[FD] child-reaped=1"),
        ("parent cwd kept", "[FD] parent-cwd-kept=1"),
        ("relative path after fork", "[FD] relative-after-fork=1"),
        ("stdout fd", "[FD] stdout-fd=1"),
        ("stderr fd", "[FD] stderr-fd=1"),
        ("in-process checks", "[FD] done=1"),
        ("exit zero", "(filedemo) exited code=0x00000000"),
    ]
    for name, needle in expected:
        check(name, wait_since(needle, 20.0, name))

    mark()
    type_text("run errtest\n")
    errno_expected = [
        ("missing open", "[ERR] missing-open=1"),
        ("directory open", "[ERR] directory-open=1"),
        ("bad fd close", "[ERR] bad-close=1"),
        ("bad fd read", "[ERR] bad-read-fd=1"),
        ("missing chdir", "[ERR] missing-chdir=1"),
        ("mkdir create", "[ERR] mkdir-create=1"),
        ("mkdir exists", "[ERR] mkdir-exists=1"),
        ("file create", "[ERR] file-create=1"),
        ("file write", "[ERR] file-write=1"),
        ("file not directory", "[ERR] file-not-dir=1"),
        ("bad read pointer", "[ERR] bad-read-pointer=1"),
        ("missing stat", "[ERR] missing-stat=1"),
        ("bad stat pointer", "[ERR] bad-stat-pointer=1"),
        ("cwd range", "[ERR] cwd-range=1"),
        ("pipe create", "[ERR] pipe-create=1"),
        ("pipe no reader", "[ERR] pipe-no-reader=1"),
        ("missing executable", "[ERR] missing-exec=1"),
        ("missing process", "[ERR] missing-kill=1"),
        ("missing child", "[ERR] missing-child=1"),
        ("errno checks", "[ERR] done=1"),
        ("errno process exit", "(errtest) exited code=0x00000000"),
    ]
    for name, needle in errno_expected:
        check(name, wait_since(needle, 20.0, name))

    mark()
    type_text("run fdstdin\n")
    check("stdin: program starts", wait_since("[FD] stdin-ready", 10.0, "stdin-ready"))
    type_text("hello\n")
    check("stdin: read fd 0", wait_since("[FD] stdin=hello", 10.0, "stdin-data"))
    check("stdin: newline delivered",
          wait_for(lambda: "[FD] stdin-pass" in since(), 10.0, "stdin-pass"))
    check("stdin: exit zero",
          wait_since("(fdstdin) exited code=0x00000000", 10.0, "stdin-exit"))

    mark()
    type_text("run fdpipe\n")
    pipe_expected = [
        ("fork shares file offset", "[PIPE] child-read=1"),
        ("offset advances across fork", "[PIPE] shared-offset=1"),
        ("pipe create", "[PIPE] pipe-create=1"),
        ("pipe data and EOF", "[PIPE] pipe-data=1"),
        ("second pipe create", "[PIPE] pipe-create-2=1"),
        ("dup2 redirects stdout", "[PIPE] dup2-stdout=1"),
        ("pipe in-process checks", "[PIPE] done=1"),
        ("pipe process exit", "(fdpipe) exited code=0x00000000"),
    ]
    for name, needle in pipe_expected:
        check(name, wait_since(needle, 20.0, name))

    mark()
    type_text("ls /fdhome\n")
    check("shell lists user-created directory",
          wait_for(lambda: "[SH] ls: note.txt" in since(), 10.0, "ls"))
    mark()
    type_text("cat /fdhome/note.txt\n")
    check("shell reads file through filesystem",
          wait_for(lambda: "[SH] cat: kernel fd" in since(), 10.0, "cat"))
    mark()
    type_text("pwd\n")
    check("shell cwd remains process-local",
          wait_for(lambda: "[SH] pwd: /\n" in since(), 10.0, "pwd"))
finally:
    try:
        qemu.terminate()
    except Exception:
        pass
    qemu.wait(timeout=5)
    serial_file.close()

print()
print("SUMMARY:", "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
sys.exit(0 if results and all(ok for _, ok in results) else 1)
