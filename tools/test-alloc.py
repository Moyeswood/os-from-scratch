#!/usr/bin/env python3
# 里程碑 13 测试: 用户态空闲链表 allocator
#   - malloc 返回对齐且不重叠的块, free 后可复用；
#   - 相邻空闲块合并, 堆尾释放后通过负 sbrk 收缩；
#   - calloc 清零与乘法溢出检查；
#   - realloc 扩容保留内容, 缩容原地完成, 0 长度等价 free。
import os
import socket
import subprocess
import sys
import threading
import time

IMG = "build/os.img"
MON = "/tmp/newos-alloc-mon.sock"
SERIAL = "/tmp/newos-alloc-serial.log"
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
        while True:
            data = sock.recv(65536)
            if not data:
                break
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


def type_text(text, wait=0.1):
    for ch in text:
        if ch == " ":
            send("sendkey spc", wait=wait)
        elif ch == "\n":
            send("sendkey ret", wait=wait)
        else:
            send(f"sendkey {ch}", wait=wait)


def serial_log():
    return serial_buf.decode(errors="replace")


serial_base = 0


def mark_serial_base():
    global serial_base
    serial_base = max(0, len(serial_buf) - 64)


def serial_since():
    return serial_log()[serial_base:]


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
    print(f"  [wait_for:{name}] timeout: tail={serial_log()[-400:]!r}", flush=True)
    return False


def wait_since(needle, timeout=10.0, name=""):
    return wait_for(lambda: needle in serial_since(), timeout, name)


try:
    time.sleep(3.0)
    check("boot: scheduler marker", "[TASK] scheduler" in serial_log())
    check("boot: splash done", wait_for(
        lambda: "[SP] done" in serial_log() or "[SP] skipped" in serial_log(),
        timeout=15.0, name="splash-done"))
    time.sleep(0.8)

    mark_serial_base()
    type_text("run allocdemo\n")
    check("allocdemo: spawn succeeds",
          wait_since("[PROC] spawn allocdemo", name="spawn"))
    check("alloc: allocations succeed",
          wait_since("[ALLOC] alloc-ok=1", name="alloc-ok"))
    check("alloc: returned pointers are aligned",
          wait_since("[ALLOC] align-ok=1", name="align"))
    check("free: break stays fixed",
          wait_since("[ALLOC] free-keeps-brk=1", name="free-brk"))
    check("free: freed block is reused",
          wait_since("[ALLOC] free-reuses-block=1", name="reuse"))
    check("free: reuse avoids sbrk growth",
          wait_since("[ALLOC] reuse-keeps-brk=1", name="reuse-brk"))
    check("coalesce: adjacent free blocks merge",
          wait_since("[ALLOC] adjacent-coalesce=1", name="coalesce"))
    check("coalesce: merged block meets larger request",
          wait_since("[ALLOC] coalesce-keeps-brk=1", name="coalesce-brk"))
    check("tail: freeing tail shrinks the break",
          wait_since("[ALLOC] tail-shrink=1", name="tail-shrink"))
    check("calloc: memory is zeroed",
          wait_since("[ALLOC] calloc-zero=1", name="calloc-zero"))
    check("calloc: multiplication overflow is rejected",
          wait_since("[ALLOC] calloc-overflow=1", name="calloc-overflow"))
    check("realloc: expansion preserves contents",
          wait_since("[ALLOC] realloc-preserves=1", name="realloc-copy"))
    check("realloc: shrink happens in place",
          wait_since("[ALLOC] realloc-shrinks-in-place=1", name="realloc-shrink"))
    check("realloc: in-place shrink keeps the break",
          wait_since("[ALLOC] in-place-keeps-brk=1", name="in-place-brk"))
    check("realloc: freeing final block shrinks the break",
          wait_since("[ALLOC] realloc-tail-shrink=1", name="realloc-tail"))
    check("realloc(NULL, n) behaves like malloc",
          wait_since("[ALLOC] realloc-null-allocates=1", name="realloc-null"))
    check("realloc(p, 0) behaves like free",
          wait_since("[ALLOC] realloc-zero-frees=1", name="realloc-zero"))
    check("realloc(p, 0) shrinks the heap tail",
          wait_since("[ALLOC] realloc-zero-shrinks=1", name="realloc-zero-shrink"))
    check("stress: mixed-size blocks remain intact and non-overlapping",
          wait_since("[ALLOC] stress-integrity=1", name="stress-integrity"))
    check("stress: freeing all blocks returns the heap tail",
          wait_since("[ALLOC] stress-shrink=1", name="stress-shrink"))
    check("allocdemo: all in-process checks pass",
          wait_since("[ALLOC] done=1", name="done"))
    check("allocdemo: exits zero",
          wait_since("(allocdemo) exited code=0x00000000", name="exit"))
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
