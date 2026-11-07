# 里程碑 22：暂停、恢复与完整作业表

更新时间：2026-11-07

## 目标

里程碑 21 已实现进程组、前台 Ctrl+C 和 `jobs/fg/kill/wait`，但作业一旦
启动就只能继续运行或终止。里程碑 22 补齐可恢复的生命周期：

```text
RUNNING
  → Ctrl+Z / SIGSTOP
  → STOPPED
  → bg %n / fg %n
  → RUNNING
```

## 1. TASK_STOPPED

调度器状态机新增：

```c
typedef enum {
    TASK_IDLE,
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_STOPPED,
    TASK_ZOMBIE
} task_state_t;
```

`TASK_STOPPED` 不参与 `pick_next()` 轮转，但进程仍保留：

- PCB、pid、pgid 和父进程关系
- 页目录、用户栈、堆和 mmap
- fd 表和当前 CWD
- 中断时保存的上下文帧

因此恢复时不需要重新加载程序或重新创建地址空间。

## 2. SIGSTOP / SIGCONT

`proc_signal()` 现在分三类处理：

| 信号 | 动作 |
|---|---|
| `SIGINT/SIGTERM/SIGKILL` | 标为 ZOMBIE，唤醒等待父进程 |
| `SIGSTOP` | READY/RUNNING/SLEEPING → STOPPED |
| `SIGCONT` | STOPPED → READY，清除旧的等待原因 |

信号可发送给单个 pid，也可通过负 pid 发送给整个进程组：

```c
kill(-pgid, SIGSTOP);
kill(-pgid, SIGCONT);
```

停止和继续都会唤醒 `TASK_WAIT_PROC` 的父进程，使它重新检查子进程状态。

当前实现把被暂停的进程在 SIGCONT 后直接置为 READY。如果进程原本睡在
pipe 或时间等待中，它会从阻塞系统调用内部重新检查条件；这是教学版语义，
还不是 POSIX 的完整信号挂起队列。

## 3. Ctrl+Z

键盘 IRQ1 同时识别：

```text
Ctrl+C → SIGINT   → 终止前台进程组
Ctrl+Z → SIGSTOP  → 暂停前台进程组
```

前台作业存在时，这两个组合键不会进入普通字符输入缓冲。

## 4. waitpid WUNTRACED

新增：

```c
#define WNOHANG   1
#define WUNTRACED 2
```

`waitpid` 行为：

- 优先返回并回收 ZOMBIE。
- 传入 `WUNTRACED` 时，遇到 STOPPED 子进程立即返回其 pid。
- 暂停子进程不回收、不释放地址空间。
- WNOHANG 仍保持非阻塞语义。

状态编码：

```text
正常退出: status = exit_code
暂停:     status = (SIGSTOP << 8) | 0x7F
```

用户库提供：

```c
WIFSTOPPED(status)
WSTOPSIG(status)
WEXITSTATUS(status)
```

## 5. 作业表升级

前台流水线现在也登记在作业表中。正常退出后会立即清理；收到 SIGSTOP 时保留：

```text
[JOB] stopped 1: spinner &
sh:/> jobs
[JOB] 1 STOPPED spinner &
```

内建命令：

| 命令 | 行为 |
|---|---|
| `jobs` | 显示 RUNNING / STOPPED / DONE |
| `fg %n` | 必要时 SIGCONT，设为前台并等待下一次停止或退出 |
| `bg %n` | 对 STOPPED 作业发送 SIGCONT，保留在后台 |
| `kill %n` | 终止整组，包括暂停中的进程 |
| `wait` | 等待运行中作业；遇到暂停作业时返回 |

作业编号使用当前空闲槽位 `%1..%4`，作业回收后编号可复用，与常见 Shell
的作业编号行为一致。

## 6. 测试器修复

`test-shell.py` 原来把字节偏移 `base` 用于解码后的 Unicode 字符串切片。
当串口日志包含较长的中文输出时，字节与字符索引会错位，造成测试假失败。
现在改为先在字节缓冲 `serial_buf[base:]` 上切片，再解码。

## 7. 验证

新增测试覆盖：

- 后台作业 `fg` 后 Ctrl+Z
- 内核 SIGSTOP 标记与 Shell STOPPED 输出
- `jobs` 显示 STOPPED
- `bg` 恢复后台运行
- 再次 `fg` 并 Ctrl+C 终止
- 暂停/终止后作业槽位回收与编号复用
- 原有四段管道、重定向和 kill/wait 路径

`test-shell.py` 现为 59 项。完整回归：

```bash
make test-suite
```

18 套测试、文档统计 535 项全部通过。最终 `kernel.bin` 为 180892 字节，
低于 184320 字节的引导加载上限。

## 8. 当前限制

- 信号没有用户态处理函数帧和返回值。
- SIGSTOP/SIGCONT 不维护待处理信号队列。
- 时间睡眠在被暂停后会提前结束，而不是保留剩余 deadline。
- 没有 session、控制终端设备对象和用户权限检查。
- 作业表只存在于当前用户态 Shell，最多 4 个后台/暂停作业。
- 仍不支持引号、转义、变量展开、通配符和 `;`。

## 9. 下一步

文件系统和进程控制目前都已有较完整的教学闭环。下一步更建议转向
**持久化磁盘文件系统**：

1. ATA PIO 扇区读写。
2. 将现有超级块/inode/目录格式落盘。
3. mount/fsck 与读写缓存。
4. 断电重启后文件仍可恢复。
5. 后续在持久化 FS 上补充硬链接、rename、stat 等语义。
