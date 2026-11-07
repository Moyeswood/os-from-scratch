# 里程碑 21：多级管道、进程组与作业控制

更新时间：2026-11-07

## 目标

里程碑 20 的用户态 Shell 已经支持两段管道、重定向和后台 `&`，但还没有：

- 多级管道 `a | b | c`
- 进程组
- 终端前台作业
- Ctrl+C 中断
- 后台作业表

里程碑 21 把这些机制串成一条完整链路：

```text
Shell fork 整条流水线
  → 所有子进程加入同一 pgid
  → tcsetpgrp(pgid)
  → Ctrl+C
  → 键盘 IRQ 向 -pgid 发送 SIGINT
  → 整组进程退出
  → Shell waitpid 并恢复提示符
```

## 1. 进程组

PCB 新增：

```c
int pgid;
```

规则：

- 新程序默认 `pgid = pid`。
- `fork` 子进程先继承父进程组。
- Shell 在父子两侧都调用 `setpgid`，避免 fork 后调度竞态。
- 一条管道中的所有命令共享同一个 `pgid`。
- 不同管道和后台作业拥有不同进程组。

新增系统调用：

| 调用 | 作用 |
|---|---|
| `SYS_SETPGID` | 把子进程加入指定组 |
| `SYS_TCSETPGRP` | 设置控制台前台进程组 |
| `SYS_KILL` | 向单进程或负 pid 表示的进程组发送终止信号 |

当前支持 `SIGINT`、`SIGKILL`、`SIGTERM`。它们都采用教学版“直接终止”语义，
尚未建立用户态信号处理函数帧。

## 2. Ctrl+C

键盘 IRQ1 现在独立跟踪 Ctrl 按下状态。若前台组存在，Ctrl+C 不再进入普通
输入缓冲区，而是：

```text
proc_signal_foreground(SIGINT)
  → proc_signal(-console_fg_pgid, SIGINT)
  → 遍历 PCB，将同组用户进程标为 ZOMBIE
  → task_wake_pid 唤醒等待它们的 Shell
```

Shell 在等待前台流水线期间保持其 pgid 为控制台前台组。全部子进程退出后，
Shell 调用 `tcsetpgrp(0)`，重新获得普通提示符。

这不是完整的 POSIX 终端层：没有 session、控制终端设备对象或用例组认证。
它保留了最小闭环，同时足以展示键盘中断、进程组和等待状态之间的配合。

## 3. 多级管道

Shell 的 `PIPE_MAX` 从 2 提升到 4，因此支持最多四段命令：

```text
echo hello | cat | cat | cat
```

创建流水线前一次性分配 `count - 1` 个 pipe。每个子进程：

1. 加入共同 pgid。
2. 把上一段 pipe 读端 `dup2` 到 fd 0。
3. 把下一段 pipe 写端 `dup2` 到 fd 1。
4. 关闭所有临时 pipe fd。
5. 应用 `<`、`>`、`>>`。
6. 执行内建命令或 `execve`。

三根 pipe 需要父进程同时持有 6 个临时 fd，加上 fd 0/1/2 后超过旧上限 8；
因此每进程 fd 上限提升到 16。

## 4. 后台作业表

用户态 Shell 维护最多 4 个作业：

```c
typedef struct {
    int id;
    int pgid;
    int pids[PIPE_MAX];
    char text[JOB_TEXT];
} job_t;
```

每个作业记录整条管道的所有 pid。Shell 在每次读取下一行命令前使用
`waitpid(-1, WNOHANG)` 回收已退出子进程，并清除作业中对应 pid。

新增 Shell 内建：

| 命令 | 行为 |
|---|---|
| `jobs` | 显示 RUNNING/DONE，并清理已结束作业 |
| `fg %n` | 把作业组设为前台并等待 |
| `kill [-SIG] %n` | 向作业组发送信号 |
| `kill [-SIG] pid` | 向单进程发送信号 |
| `wait` | 等待并清理全部后台作业 |

示例：

```text
sh:/> spinner &
[JOB] started 1: spinner &
sh:/> jobs
[JOB] 1 RUNNING spinner &
sh:/> fg %1
[JOB] foreground 1
^C
sh:/>
```

## 5. 进程终止修正

此前 `proc_kill` 只把目标标成 ZOMBIE，没有唤醒 `TASK_WAIT_PROC` 的父进程。
后台 kill 测试不经过前台 wait，因此旧问题没有暴露。里程碑 21 把终止逻辑
统一到 `kill_task()`，并在状态转换后调用 `task_wake_pid()`；Ctrl+C 和组信号
因此可以立即唤醒 Shell。

## 6. 构建依赖修正

新增 PCB 字段后，第一次增量构建暴露了旧问题：Makefile 的 `.o` 规则只依赖
`.c`，修改头文件里的结构体会留下旧偏移的目标文件。现在所有内核 `.o` 都依赖
`kernel/*.h`，头文件变化会自动触发完整重建。

## 7. 验证

`test-shell.py` 扩展到 47 项，新增：

- 四段管道数据传递
- 前台 spinner
- Ctrl+C 信号与提示符恢复
- `jobs` 显示进程组
- `kill %n` + `wait` 作业回收
- 后台作业重新用 `fg %n` 拉回前台
- 恢复后的作业再次 Ctrl+C

完整回归：

```bash
make test-suite
```

18 套测试、文档统计 523 项全部通过。最终 `kernel.bin` 为 178908 字节，
仍低于 180KB 引导上限。

## 8. 当前限制

- 信号只支持默认终止，不支持自定义处理函数或恢复现场。
- 尚无 `SIGSTOP/SIGCONT`，因此没有 Ctrl+Z、暂停作业和 `bg`。
- 管道最多四段。
- 作业表只存在于当前用户态 Shell 会话，最多 4 个后台作业。
- 没有 session、控制终端对象、用户权限检查或完整 POSIX 终端语义。
- 仍不支持引号、转义、变量展开、通配符和 `;`。

## 9. 下一步

里程碑 22 建议实现：

1. `SIGSTOP/SIGCONT` 与 Ctrl+Z 暂停前台作业。
2. `waitpid(..., WUNTRACED)` 和停止状态。
3. `bg %n` 与暂停作业恢复。
4. 最小用户态信号处理回调。
5. 之后可转向持久化磁盘文件系统或网络设备。
