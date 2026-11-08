# 里程碑 26：用户态信号

## 目标

把原先只有“默认终止/暂停/继续”的进程控制，扩展成可以在 Ring 3
安装处理函数、屏蔽信号并从 handler 返回完整用户现场的机制。

## 支持范围

| 信号 | 默认动作 | 可安装 handler | 可屏蔽 |
|---|---|---|---|
| `SIGINT` / 2 | 终止 | 是 | 是 |
| `SIGTERM` / 15 | 终止 | 是 | 是 |
| `SIGKILL` / 9 | 终止 | 否 | 否 |
| `SIGSTOP` / 19 | 暂停 | 否 | 否 |
| `SIGCONT` / 18 | 继续 | 是 | 是 |

支持 `SIG_DFL`、`SIG_IGN`、`SIG_BLOCK`、`SIG_UNBLOCK` 和
`SIG_SETMASK`。

## 新增系统调用

```text
SYS_SIGACTION   32(sig, handler, restorer)
SYS_SIGPROCMASK 33(how, set, oldset)
SYS_SIGRETURN   34()
```

用户库提供：

```c
void handler(int sig);
signal(SIGINT, handler);
sigprocmask(SIG_BLOCK, &set, &old);
```

## 待处理状态

每个 `task_t` 保存：

- `signal_pending`：32 bit 待处理位图；
- `signal_mask`：32 bit 屏蔽位图；
- `signal_handlers[32]`：每信号 action；
- `signal_restorer`：用户态 syscall 返回跳板地址。

fork 会复制 pending、mask、handlers 和 restorer；exec 成功后恢复默认 action。

## 用户栈 handler 帧

递送前在用户栈构造：

```text
[restorer return address]
[handler argument = signal number]
[padding]
[saved iret/user frame]
[saved signal mask + signal + magic]
```

上下文帧的 EIP 改为 handler，ESP 指向 restorer 返回地址。

handler 按普通 cdecl 函数执行 `void handler(int)`，返回后进入 naked
汇编跳板：

```asm
movl $34, %eax
int $0x80
```

`sigreturn` 校验 magic、用户页和 Ring 3 CS，然后恢复全部通用寄存器、
EIP/ESP/EFLAGS、原屏蔽字和 syscall 返回值。

## 递送时机

- 每次 `int 0x80` 返回用户态前；
- IRQ0 选中一个用户任务、准备 iret 前；
- 信号在被屏蔽期间保留 pending，解屏蔽后的下一次返回用户态时递送；
- handler 执行期间自动屏蔽当前信号。

## 测试

`user/sigtest.c` 覆盖：

1. 安装 `SIGINT` handler；
2. `kill(getpid(), SIGINT)` 自发送；
3. handler 打印并通过 `sigreturn` 回到原代码；
4. 屏蔽 `SIGTERM` 后发送，确认 pending；
5. 解屏蔽后自动递送；
6. `SIG_IGN`；
7. 第二个 handler 返回后程序继续并正常退出。

运行：

```bash
make test-signals
python3 tools/test-shell.py
```

## 当前限制

- 阻塞在键盘/pipe/wait 等系统调用内部时，信号可以在下一次用户态返回时
  递送，但还没有统一的 `EINTR` 重启语义。
- 尚无实时信号、`siginfo_t`、SA_RESTART 和信号栈 `sigaltstack`。

## 里程碑 26b：默认 Ring 3 Shell

启动扇区最后一个未使用字节保存启动模式：

```text
0x7DFD = 1  build/os.img         → Ring 3 shell
0x7DFD = 0  build/os-kernel.img  → recovery kernel shell
```

正式镜像在 idle 上下文通过前台 `user_run("sh")` 启动用户 Shell。
用户 Shell 正常退出、异常退出或被信号杀死后，`user_run()` 返回，
内核随后唤醒任务 1，进入 `OS:/>` 恢复 Shell。

测试和恢复工具统一使用 `build/os-kernel.img`，避免依赖按键时序；
正式镜像与恢复镜像由同一次内核构建生成，仅启动模式字节不同。

启动路径测试：

```bash
make test-boot-shell
```
