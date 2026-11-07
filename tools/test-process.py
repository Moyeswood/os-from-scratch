#!/usr/bin/env python3
"""Milestone 15: waitpid, argv/envp stacks, and ELF32 loading."""

import os
import re
import socket
import subprocess
import sys
import threading
import time

IMG = "build/os.img"
MON = "/tmp/newos-process-mon.sock"
SERIAL = "/tmp/newos-process-serial.log"

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


def monitor(command, wait=0.08):
    sock.settimeout(0.3)
    try:
        while sock.recv(65536):
            pass
    except socket.timeout:
        pass
    sock.settimeout(10)
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


def type_text(text, wait=0.09):
    for char in text:
        if char == " ":
            monitor("sendkey spc", wait)
        elif char == "\n":
            monitor("sendkey ret", wait)
        elif char == "/":
            monitor("sendkey slash", wait)
        elif char == "-":
            monitor("sendkey minus", wait)
        else:
            monitor(f"sendkey {char}", wait)


def log():
    return serial_data.decode(errors="replace")


base = 0


def mark():
    global base
    base = max(0, len(serial_data) - 64)


def since():
    return log()[base:]


results = []


def check(name, condition, detail=""):
    results.append((name, bool(condition)))
    print(f"{name}: {'PASS' if condition else 'FAIL'} {detail}", flush=True)


def wait_for(condition, timeout=12.0, name=""):
    started = time.time()
    while time.time() - started < timeout:
        if condition():
            return True
        time.sleep(0.15)
    print(f"  timeout {name}: {log()[-400:]!r}", flush=True)
    return False


def wait_since(needle, timeout=12.0, name=""):
    return wait_for(lambda: needle in since(), timeout, name)


check("boot: flat programs installed", wait_for(
    lambda: "[EXEC] installed 0x00000012/0x00000012 programs in /bin" in log(),
    15, "flat-install"))
check("boot: ELF demo installed", wait_for(
    lambda: "[EXEC] installed ELF /bin/elfdemo bytes=" in log(),
    15, "elf-install"))
check("boot: splash completed", wait_for(
    lambda: "[SP] done" in log() or "[SP] skipped" in log(), 20, "splash"))
time.sleep(1.0)

# 首次加载 ELF 可能让内核堆扩容。先跑一次把内核侧缓存路径预热，
# 之后的内存比较只衡量进程页表/页面是否回收。
mark()
type_text("run /bin/elfdemo warmup warmup\n")
check("ELF: warm-up run completes",
      wait_since("[USER] exit code=0x00000000", 15, "elf-warmup"))

mark()
type_text("meminfo\n")
check("memory baseline command", wait_since("[SH] meminfo pages=", 10, "mem-before"))
match = re.search(
    r"\[SH\] meminfo pages=0x([0-9A-F]+) used=0x([0-9A-F]+) free=0x([0-9A-F]+)",
    since())
used_before = int(match.group(2), 16) if match else -1

mark()
type_text("run waittest\n")
check("waitpid: specific child reaped",
      wait_since("[WAIT] specific-pid=", 12, "wait-specific"))
check("waitpid: specific exit status=42",
      wait_since("[WAIT] specific-status=42", 12, "wait-status"))
check("waitpid: any-child selector",
      wait_since("[WAIT] any-pid=", 12, "wait-any"))
check("waitpid: any-child exit status=7",
      wait_since("[WAIT] any-status=7", 12, "wait-any-status"))
check("waitpid: no remaining child returns -1",
      wait_since("[WAIT] no-child-ok=1", 12, "wait-none"))
check("waitpid: rejected status pointer",
      wait_since("[SYS] waitpid: rejected status pointer", 12, "wait-badptr") and
      "[WAIT] badptr-ok=1" in since())
check("waitpid: parent state coherent",
      wait_since("[WAIT] done=1", 12, "wait-done"))
check("waitpid: parent exits cleanly",
      wait_since("[USER] exit code=0x00000000", 12, "wait-exit"))

# 后台父进程睡眠时，子进程先变僵尸。Shell 启动另一个程序会触发
# 孤儿清扫；活父进程的孩子必须保留到 waitpid。
mark()
type_text("run waittest &\n")
check("detached: background parent starts",
      wait_since("[PROC] spawn waittest", 12, "bg-wait-spawn"))
bg = re.search(r"\[PROC\] spawn waittest -> pid 0x([0-9A-F]+)", since())
bg_pid = bg.group(1) if bg else ""
check("detached: parent has a zombie child",
      wait_since("(waittest) exited code=0x0000002A", 12, "bg-child-zombie"))
type_text("run hello\n")
check("detached: unrelated spawn provides sweep window",
      wait_since("Hello from Ring 3!", 12, "bg-hello"))
check("detached: live parent's zombie survives sweep",
      wait_since("[WAIT] specific-status=42", 12, "bg-specific") and
      "[PROC] swept orphan zombie" not in since())
check("detached: background parent completes",
      wait_since("[WAIT] done=1", 12, "bg-done"))
check("detached: background parent remains reclaimable", wait_for(
    lambda: bool(bg_pid) and
            f"[PROC] pid 0x{bg_pid} (waittest) exited code=0x00000000" in since(),
    12, "bg-exit"))

mark()
type_text("run /bin/elfdemo alpha beta\n")
check("ELF: loader detects format", wait_since("[ELF] load pid=", 12, "elf-load"))
elf_line = re.search(
    r"\[ELF\] load pid=0x([0-9A-F]+) entry=0x([0-9A-F]+) segments=0x([0-9A-F]+)",
    since())
check("ELF: two PT_LOAD segments mapped", bool(elf_line) and elf_line.group(3) == "00000002")
check("ELF: argv count delivered",
      wait_since("[ELF-DEMO] argc=3", 12, "elf-argc"))
check("ELF: argv strings delivered",
      wait_since("[ELF-DEMO] argv1=alpha", 12, "elf-argv1") and
      "[ELF-DEMO] argv2=beta" in since())
check("ELF: default environment delivered",
      wait_since("[ELF-DEMO] env0=HOME=/", 12, "elf-env"))
check("ELF: initialized data segment",
      wait_since("[ELF-DEMO] data=7", 12, "elf-data"))
check("ELF: NOBITS/BSS zero-filled",
      wait_since("[ELF-DEMO] bss-zero=1", 12, "elf-bss"))
check("ELF: program succeeds",
      wait_since("[ELF-DEMO] done=1", 12, "elf-done"))
check("ELF: process exits cleanly",
      wait_since("[USER] exit code=0x00000000", 12, "elf-exit"))

mark()
type_text("run execargv\n")
check("execve: process starts", wait_since("[EXEC-ARGV] replacing", 12, "execve-start"))
spawn = re.search(r"\[PROC\] spawn execargv -> pid 0x([0-9A-F]+)", since())
check("execve: old pid captured", bool(spawn))
old_pid = spawn.group(1) if spawn else ""
check("execve: ELF target loaded",
      wait_since("[EXEC] file path=/bin/elfdemo", 12, "execve-file") and
      "[ELF] load pid=" in since())
check("execve: PID preserved", wait_for(
    lambda: bool(old_pid) and
            f"[EXEC] replace pid=0x{old_pid}" in since(), 12, "execve-pid"))
check("execve: argv vector delivered",
      wait_since("[ELF-DEMO] argv1=from-exec", 12, "execve-argv1") and
      "[ELF-DEMO] argv2=env-test" in since())
check("execve: envp vector delivered",
      wait_since("[ELF-DEMO] env0=ORIGIN=exec", 12, "execve-env"))
check("execve: replacement exits cleanly",
      wait_since("[USER] exit code=0x00000000", 12, "execve-exit"))

mark()
type_text("run /bin/hello\n")
check("compatibility: flat file image still loads",
      wait_since("[EXEC] file path=/bin/hello", 12, "flat-file") and
      "[ELFF" not in since())
check("compatibility: flat file executes",
      wait_since("Hello from Ring 3!", 12, "flat-run"))

mark()
type_text("meminfo\n")
check("memory stats after process tests", wait_since("[SH] meminfo pages=", 10, "mem-after"))
match = re.search(
    r"\[SH\] meminfo pages=0x([0-9A-F]+) used=0x([0-9A-F]+) free=0x([0-9A-F]+)",
    since())
used_after = int(match.group(2), 16) if match else -2
check("memory: wait/ELF/exec pages fully reclaimed",
      used_before == used_after, f"before={used_before} after={used_after}")

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
