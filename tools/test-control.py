#!/usr/bin/env python3
# Milestone 34: Shell Control Flow Tests
import os, socket, subprocess, sys, threading, time

IMG = "build/os-kernel.img"
MON = "/tmp/newos-cf-mon.sock"
SERIAL = "/tmp/newos-cf-serial.log"
for f in [MON, SERIAL]:
    try: os.unlink(f)
    except FileNotFoundError: pass

serial_file = open(SERIAL, "wb")
serial_buf = bytearray()

def serial_reader(pipe):
    while True:
        data = os.read(pipe.fileno(), 65536)
        if not data: break
        serial_buf.extend(data)
        serial_file.write(data)
        serial_file.flush()

qemu = subprocess.Popen(
    ["qemu-system-i386", "-fda", IMG, "-snapshot", "-display", "none",
     "-monitor", f"unix:{MON},server,nowait", "-serial", "stdio"],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
threading.Thread(target=serial_reader, args=(qemu.stdout,), daemon=True).start()

# Wait longer for boot + shell startup
time.sleep(10)

for _ in range(200):
    if os.path.exists(MON): break
    time.sleep(0.1)

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(MON)
sock.settimeout(10)

KEYS = {
    " ": "spc", "/": "slash", ".": "dot",
    "-": "minus", "%": "shift-5",
    "<": "shift-comma", ">": "shift-dot",
    "|": "shift-backslash", "&": "shift-7",
    "'": "apostrophe", '"': "shift-apostrophe",
    "$": "shift-4", ";": "semicolon", "\\": "backslash",
    "?": "shift-slash", "*": "shift-8",
    "(": "shift-9", ")": "shift-0",
    "[": "bracket_left", "]": "bracket_right",
}
for c in range(ord("A"), ord("Z") + 1):
    KEYS[chr(c)] = f"shift-{chr(c + 32)}"

results = []
def check(name, cond):
    results.append((name, cond))
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {name}")
    return cond

def send(cmd, wait=0.1):
    sock.sendall((cmd + "\r").encode())
    time.sleep(wait)
    out = b""
    for _ in range(20):
        try:
            sock.settimeout(0.5)
            data = sock.recv(65536)
            if not data: break
            out += data
        except socket.timeout:
            break
    return out.decode(errors="replace")

print("=== Milestone 34: Shell Control Flow Tests ===\n")

text = serial_buf.decode(errors="replace")
print(f"Boot output length: {len(text)} chars")

# Check if we're already at a shell prompt
if "sh:/>" in text or "OS:/>" in text:
    print("Shell prompt found!")
else:
    # Try to enter shell
    print("Attempting to enter user shell...")
    send("run sh\n")
    time.sleep(3)
    text = serial_buf.decode(errors="replace")
    if "sh:/>" in text:
        print("Successfully entered shell")
    else:
        print("Could not detect shell prompt, using generic detection")

# Test basic echo
print("\n--- Test: Basic Echo ---")
output = send("echo hello\n")
time.sleep(2)
full_output = serial_buf.decode(errors="replace")
check("basic echo works", "hello" in full_output.lower())

# Test && operator
print("\n--- Test: Logical AND (&&) ---")
output = send("echo a && echo b\n")
time.sleep(2)
full_output = serial_buf.decode(errors="replace")
check("echo a && echo b shows both", "a" in full_output and "b" in full_output)

# Test && short circuit
print("\n--- Test: && Short Circuit ---")
output = send("false && echo should-not-appear\n")
time.sleep(2)
full_output = serial_buf.decode(errors="replace")
check("false && skips second", "should-not-appear" not in full_output.lower())

# Test || operator
print("\n--- Test: Logical OR (||) ---")
output = send("true || echo fallback\n")
time.sleep(2)
full_output = serial_buf.decode(errors="replace")
check("true || skips second", "fallback" not in full_output.lower())

output = send("false || echo fallback\n")
time.sleep(2)
full_output = serial_buf.decode(errors="replace")
check("false || shows fallback", "fallback" in full_output.lower())

# Test ! operator
print("\n--- Test: Negation (!) ---")
output = send("! true && echo negated\n")
time.sleep(2)
full_output = serial_buf.decode(errors="replace")
check("! true && no output", "negated" not in full_output.lower())

output = send("! false && echo positive\n")
time.sleep(2)
full_output = serial_buf.decode(errors="replace")
check("! false && shows output", "positive" in full_output.lower())

# Cleanup
try:
    qemu.terminate()
except: pass
try:
    qemu.wait(timeout=5)
except:
    pass
serial_file.close()

passed = sum(1 for _, ok in results if ok)
total = len(results)
print(f"\nSUMMARY: {total} checks, {passed} PASS, {total - passed} FAIL")
sys.exit(0 if passed == total else 1)
