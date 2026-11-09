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
    "'": "apostrophe", '"': "shift-apostrophe",
    "$": "shift-4", ";": "semicolon", "\\": "backslash",
    "?": "shift-slash", "*": "shift-8",
    "(": "shift-9", ")": "shift-0",
}
KEYS.update({chr(c): f"shift-{chr(c + 32)}" for c in range(ord("A"), ord("Z") + 1)})
KEYS["="] = "equal"


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
    run('echo "double quoted"', "sh:/shdir>", "double quoted\n",
        name="double quotes")
    run("echo '$HOME'", "sh:/shdir>", "$HOME\n", name="single quotes")
    run('echo "$HOME"', "sh:/shdir>", "/\n", name="variable expansion")
    run("NAME=world", "sh:/shdir>", name="local assignment")
    run("echo hello-$NAME", "sh:/shdir>", "hello-world\n",
        name="local variable expansion")
    run("export NAME", "sh:/shdir>", name="export variable")
    run("env", "sh:/shdir>", "NAME=world\n",
        name="exported variable reaches env")
    run("TEMP=temp env", "sh:/shdir>", "TEMP=temp\n",
        name="temporary command environment")
    run("echo x$TEMP", "sh:/shdir>", "x\n",
        name="temporary environment restored")
    run("unset NAME", "sh:/shdir>", name="unset variable")
    run("echo after-$NAME", "sh:/shdir>", "after-\n",
        name="unset removes variable")
    run("echo value=$(echo nested)", "sh:/shdir>", "value=nested\n",
        name="command substitution")
    run("echo $(echo a; echo b)", "sh:/shdir>", "a b\n",
        name="multiline command substitution")
    run("echo $(echo $(echo nested2))", "sh:/shdir>", "nested2\n",
        name="nested command substitution")
    run("echo $(echo '$HOME')", "sh:/shdir>", "$HOME\n",
        name="substitution output is not re-expanded")
    run("echo $(echo 'a;b')", "sh:/shdir>", "a;b\n",
        name="substitution output operators stay literal")
    run("echo '$(echo literal)'", "sh:/shdir>", "$(echo literal)\n",
        name="quoted command substitution")
    run("sh -c \"echo dash-c\"", "sh:/shdir>", "dash-c\n",
        name="shell dash-c command")
    run("echo escaped\\ space", "sh:/shdir>", "escaped space\n",
        name="backslash escape")
    run('echo "quoted;literal"', "sh:/shdir>", "quoted;literal\n",
        name="quoted semicolon")
    run("false; echo status=$?", "sh:/shdir>", "status=1\n",
        name="command sequence and status")
    run("nosuch-command 2> err.txt; cat err.txt", "sh:/shdir>",
        "sh: not found: nosuch-command\n", name="stderr redirect")
    run("nosuch-command 2>&1 | cat", "sh:/shdir>",
        "sh: not found: nosuch-command\n", name="stderr to stdout")
    run("ls", "sh:/shdir>", "a.txt\n", name="ls utility")
    run("echo beta > beta.txt", "sh:/shdir>", name="glob setup beta")
    run("echo alpha > alpha.txt", "sh:/shdir>", name="glob setup alpha")
    run("echo markdown > note.md", "sh:/shdir>", name="glob setup note")
    run("echo *.txt", "sh:/shdir>", "a.txt alpha.txt beta.txt err.txt\n",
        name="wildcard expands and sorts")
    run("echo note.?d", "sh:/shdir>", "note.md\n",
        name="wildcard question mark")
    run('echo "*.txt"', "sh:/shdir>", "*.txt\n",
        name="quoted wildcard stays literal")
    run("echo /bin/sh*", "sh:/shdir>", "/bin/sh\n",
        name="absolute wildcard prefix")

    mark()
    type_text('echo "multi')
    type_text("\n")
    type_text('line"')
    type_text("\n")
    check("multiline quoted input", wait_since("multi line\n", 10,
                                               "multiline"))
    check("multiline prompt returns", wait_since("sh:/shdir> ", 10,
                                                 "multiline-prompt"))

    mark()
    type_text("echo abc")
    send("sendkey home")
    for _ in range(5):
        send("sendkey right")
    type_text("X")
    send("sendkey end")
    type_text("\n")
    check("home/right/end insert", wait_since("Xabc\n", 10, "home-edit"))
    check("home edit prompt", wait_since("sh:/shdir> ", 10,
                                         "home-edit-prompt"))

    mark()
    type_text("echo ac")
    send("sendkey left")
    type_text("b")
    type_text("\n")
    check("left-arrow insert", wait_since("abc\n", 10, "left-edit"))
    check("left edit prompt", wait_since("sh:/shdir> ", 10,
                                         "left-edit-prompt"))

    mark()
    type_text("echo abc")
    send("sendkey left")
    send("sendkey delete")
    type_text("\n")
    check("delete at cursor", wait_since("ab\n", 10, "delete-edit"))
    check("delete prompt", wait_since("sh:/shdir> ", 10,
                                      "delete-edit-prompt"))

    mark()
    type_text("echo draft-restored")
    send("sendkey up")
    send("sendkey down")
    type_text("\n")
    check("history navigation restores draft",
          wait_since("draft-restored\n", 10, "draft-restore"))
    check("draft restore prompt", wait_since("sh:/shdir> ", 10,
                                             "draft-restore-prompt"))

    run("echo search-seed", "sh:/shdir>", "search-seed\n",
        name="history search seed")
    mark()
    send("sendkey ctrl-r")
    type_text("search-seed")
    send("sendkey ret")
    send("sendkey ret")
    check("ctrl-r history search", wait_since("search-seed\n", 10,
                                              "history-search"))
    check("history search prompt", wait_since("sh:/shdir> ", 10,
                                              "history-search-prompt"))

    mark()
    type_text("pw")
    send("sendkey tab")
    type_text("\n")
    check("tab completes builtin", wait_since("/shdir\n", 10, "tab-builtin"))
    check("tab builtin prompt", wait_since("sh:/shdir> ", 10,
                                           "tab-builtin-prompt"))

    mark()
    type_text("cat alph")
    send("sendkey tab")
    type_text("\n")
    check("tab completes path entry", wait_since("alpha\n", 10,
                                                 "tab-path"))
    check("tab path prompt", wait_since("sh:/shdir> ", 10,
                                        "tab-path-prompt"))

    mark()
    type_text("cat /shdir/alph")
    send("sendkey tab")
    type_text("\n")
    check("tab completes absolute path", wait_since("alpha\n", 10,
                                                    "tab-absolute"))
    check("tab absolute prompt", wait_since("sh:/shdir> ", 10,
                                            "tab-absolute-prompt"))

    mark()
    type_text("cd /shd")
    send("sendkey tab")
    type_text("\n")
    check("tab completes directory", wait_since("sh:/shdir> ", 10,
                                                "tab-directory"))
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
    run("echo \"echo script-one; echo script-two\" > /script.sh",
        "sh:/>", name="write shell script")
    run("sh /script.sh", "sh:/>", "script-one\n",
        name="execute shell script first line")
    check("execute shell script second line",
          wait_since("script-two\n", 5, "script-second"))
    mark()
    type_text("sh -c \"exit 7\"\n")
    check("sh -c propagates exit status",
          wait_since("(sh) exited code=0x00000007", 10,
                     "sh-c-exit-status"))
    check("sh -c restores shell prompt",
          wait_since("sh:/> ", 10, "sh-c-status-prompt"))
    run("echo dash-c-status=$?", "sh:/>", "dash-c-status=7\n",
        name="sh -c status reaches dollar-question")
    run("echo \"exit 9\" > /exit.sh", "sh:/>", name="write exit script")
    mark()
    type_text("sh /exit.sh\n")
    check("script propagates exit status",
          wait_since("(sh) exited code=0x00000009", 10,
                     "script-exit-status"))
    check("script exit restores shell prompt",
          wait_since("sh:/> ", 10, "script-exit-prompt"))
    run("echo script-status=$?", "sh:/>", "script-status=9\n",
        name="script status reaches dollar-question")
    run("rm /shdir/a.txt /shdir/beta.txt /shdir/alpha.txt "
        "/shdir/note.md /shdir/err.txt /bgout /script.sh /exit.sh",
        "sh:/>", name="rm utility")
    run("rm /shdir", "sh:/>", name="remove empty directory")

    run("echo history-persisted", "sh:/>", "history-persisted\n",
        name="history seed")
    mark()
    send("sendkey up")
    send("sendkey ret")
    check("up arrow recalls history", wait_since("history-persisted\n", 10,
                                                 "history-up"))
    check("history recall prompt", wait_since("sh:/> ", 10,
                                              "history-up-prompt"))
    mark()
    type_text("exit\n")
    check("exit user shell before history reload",
          wait_since("[USER] exit code=0x00000000", 15.0, "shell-exit"))
    mark()
    type_text("run sh\n")
    check("history shell restarts", wait_since("sh:/> ", 15.0,
                                               "history-shell"))
    run("history 6", "sh:/>", "echo history-persisted",
        name="history persists across shell restart")
    mark()
    type_text("exit\n")
    check("final exit user shell",
          wait_since("[USER] exit code=0x00000000", 15.0, "final-shell-exit"))
finally:
    try:
        qemu.terminate()
    except Exception:
        pass
    qemu.wait(timeout=5)
    serial_file.close()

print()
print("SUMMARY:", f"{len(results)} checks",
      "ALL PASS" if all(ok for _, ok in results) else "SOME FAILED")
sys.exit(0 if results and all(ok for _, ok in results) else 1)
