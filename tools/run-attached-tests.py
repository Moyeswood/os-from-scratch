#!/usr/bin/env python3
# 附着式测试启动器: preempt/mario 是"附着"测试 (argv[1] = monitor socket),
# 需要先起好一个引导完成的 QEMU。这里: 新镜像副本 + 等 [SP] done 后依次跑。
import socket, subprocess, time, sys, os, threading

IMG = "build/os.img"
COPY = "/tmp/newos-ring3.img"
MON = "/tmp/newos-ring3-mon.sock"
try: os.unlink(MON)
except FileNotFoundError: pass

# 新镜像副本 (避免与任何持 os.img 写锁的 QEMU 冲突)
subprocess.run(["cp", IMG, COPY], check=True)

serial_buf = bytearray()
def reader(pipe):
    fd = pipe.fileno()
    while True:
        d = os.read(fd, 65536)
        if not d: break
        serial_buf.extend(d)

qemu = subprocess.Popen(
    ["qemu-system-i386", "-fda", COPY, "-snapshot", "-display", "none",
     "-monitor", f"unix:{MON},server,nowait", "-serial", "stdio"],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
threading.Thread(target=reader, args=(qemu.stdout,), daemon=True).start()

# 等 monitor socket + 开机画面结束 (附着测试假设 shell 已就绪)
for _ in range(100):
    if os.path.exists(MON): break
    time.sleep(0.1)
t0 = time.time()
while time.time() - t0 < 30:
    s = serial_buf.decode(errors='replace')
    if "[SP] done" in s or "[SP] skipped" in s:
        time.sleep(1.0)
        break
    time.sleep(0.2)
else:
    print("LAUNCHER: timeout waiting for splash"); qemu.terminate(); sys.exit(1)

# 依次跑两个附着测试
rc = 0
for t in ["test-preempt", "test-mario"]:
    print(f"===== {t} (attached) =====", flush=True)
    r = subprocess.run(["python3", f"tools/{t}.py", MON], timeout=300)
    rc = rc or r.returncode

qemu.terminate()
try: qemu.wait(timeout=5)
except Exception: pass
sys.exit(rc)
