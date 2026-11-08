#!/usr/bin/env python3
# 里程碑 20/21: 用户态 Shell、管道、重定向与作业控制
import os
import socket
import subprocess
import sys
import threading
import time

IMG = "build/os-kernel.img"
MON = "/tmp/newos-shell-mon.sock"
SERIAL = "/tmp/newos-shell-serial.log"
try:
    os.unlink(MON)
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


KEYS = {
    " ": "spc", "/": "slash", ".": "dot",
    "-": "minus", "%": "shift-5",
    "<": "shift-comma", ">": "shift-dot",
    "|": "shift-backslash", "&": "shift-7",
}


def type_text(text, wait=0.09):
    for ch in text:
        if ch == "\n":
            send("sendkey ret", wait=wait)
        else:
            send(f"sendkey {KEYS.get(ch, ch)}", wait=wait)


def log():
    return serial_buf.decode(errors="replace")


base = 0


def mark():
    global base
    base = len(serial_buf)


def since():
    return serial_buf[base:].decode(errors="replace")


results = []


def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{name}: {'PASS' if ok else 'FAIL'} {detail}", flush=True)


def wait_for(cond, timeout=12.0, name=""):
    start = time.time()
    while time.time() - start < timeout:
        if cond():
            return True
        time.sleep(0.15)
    ok = cond()
    if not ok:
        print(f"  [wait:{name}] tail={log()[-700:]!r}", flush=True)
    return ok


def wait_since(needle, timeout=12.0, name=""):
    return wait_for(lambda: needle in since(), timeout, name)


def run(cmd, prompt, needle=None, timeout=15.0, name=None):
    mark()
    type_text(cmd + "\n")
    if needle:
        check(name or cmd, wait_since(needle, timeout, name or cmd))
    check((name or cmd) + " prompt", wait_since(prompt, timeout,
                                               (name or cmd) + " prompt"))


try:
    time.sleep(3.0)
    check("boot: scheduler", "[TASK] scheduler" in log())
    check("boot: splash", wait_for(
        lambda: "[SP] done" in log() or "[SP] skipped" in log(),
        20.0, "splash"))
    check("boot: shell installed", wait_for(
        lambda: "/bin/sh" in log() or "[EXEC] installed" in log(),
        10.0, "install"))
    time.sleep(1.0)

    mark()
    type_text("run sh\n")
    check("user shell starts", wait_since("sh:/> ", 15.0, "shell prompt"))

    run("pwd", "sh:/>", "/\n", name="pwd utility")
    run("mkdir /shdir", "sh:/>", name="mkdir utility")
    run("cd /shdir", "sh:/shdir>", name="cd builtin")
    run("echo hello world > a.txt", "sh:/shdir>", name="stdout redirect")
    run("echo second line >> a.txt", "sh:/shdir>", name="append redirect")
    run("cat a.txt", "sh:/shdir>", "hello world\nsecond line\n",
        name="cat file")
    run("cat < a.txt | cat", "sh:/shdir>", "hello world\nsecond line\n",
        name="input redirect + pipe")
    run("echo pipe-ok | cat", "sh:/shdir>", "pipe-ok\n", name="pipe")
    run("echo stages | cat | cat | cat", "sh:/shdir>", "stages\n",
        name="multi-stage pipe")
    run("ls", "sh:/shdir>", "a.txt\n", name="ls utility")
    run("cd /", "sh:/>", name="cd root")

    mark()
    type_text("spinner\n")
    check("foreground job starts", wait_since("spinner: pid=", 10.0,
                                              "foreground-start"))
    send("sendkey ctrl-c", wait=0.3)
    check("ctrl-c targets foreground group", wait_since(
        "[PROC] console signal pgid=", 10.0, "ctrl-c-signal"))
    check("ctrl-c restores shell", wait_since("sh:/> ", 10.0,
                                              "ctrl-c-prompt"))

    run("spinner &", "sh:/>", "[JOB] started 1: spinner",
        name="background job starts")
    run("jobs", "sh:/>", "[JOB] 1 RUNNING spinner",
        name="jobs lists process group")
    run("kill %1", "sh:/>", name="kill background group")
    run("wait", "sh:/>", name="wait background group")
    run("jobs", "sh:/>", "[JOB] no jobs", name="jobs clears reaped job")

    run("spinner &", "sh:/>", "[JOB] started 1: spinner &",
        name="second background job")
    mark()
    type_text("fg %1\n")
    check("fg moves job to foreground", wait_since("[JOB] foreground 1",
                                                   10.0, "fg-command"))
    send("sendkey ctrl-z", wait=0.3)
    check("ctrl-z stops foreground group", wait_since(
        "stopped by SIGSTOP", 10.0, "ctrl-z-stop"))
    check("stopped job state reported", wait_since(
        "[JOB] stopped 1: spinner &", 10.0, "stopped-report"))
    check("ctrl-z restores shell", wait_since("sh:/> ", 10.0,
                                              "ctrl-z-prompt"))
    run("jobs", "sh:/>", "[JOB] 1 STOPPED spinner &",
        name="jobs lists stopped job")
    run("bg %1", "sh:/>", "[JOB] background 1",
        name="bg resumes stopped job")
    run("jobs", "sh:/>", "[JOB] 1 RUNNING spinner &",
        name="bg job runs again")
    mark()
    type_text("fg %1\n")
    check("fg reacquires running job", wait_since("[JOB] foreground 1",
                                                  10.0, "fg-resume"))
    send("sendkey ctrl-c", wait=0.3)
    check("ctrl-c ends resumed job", wait_since(
        "[PROC] console signal pgid=", 10.0, "fg-ctrl-c"))
    check("fg ctrl-c restores shell", wait_since("sh:/> ", 10.0,
                                                 "fg-prompt"))
    run("jobs", "sh:/>", "[JOB] no jobs", name="fg job reaped")

    run("echo bg-ok > /bgout &", "sh:/>", name="background redirect")
    time.sleep(0.5)
    run("cat /bgout", "sh:/>", "bg-ok\n", name="background output")
    run("nosuch-command", "sh:/>", "sh: not found: nosuch-command",
        name="unknown command")
    run("echo broken |", "sh:/>", "sh: syntax error",
        name="syntax error")
    run("rm /shdir/a.txt /bgout", "sh:/>", name="rm utility")
    run("rm /shdir", "sh:/>", name="remove empty directory")
    mark()
    type_text("exit\n")
    check("exit user shell",
          wait_since("[USER] exit code=0x00000000", 15.0, "shell-exit"))
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
