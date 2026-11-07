# 里程碑 20：用户态 Shell、管道与重定向

更新时间：2026-11-07

## 目标

里程碑 19 已有进程级 fd 表和独立 CWD，但 fd 项仍直接保存文件偏移，
没有共享打开文件对象、管道和重定向能力。里程碑 20 把这些机制连起来：

```text
Ring 3 Shell
  → fork
  → pipe / open
  → dup2
  → execve
  → waitpid
```

内核 Shell 继续保留为救援和调试入口；用户态 Shell 通过以下命令进入：

```text
run sh
```

## 1. 共享打开文件对象

新增 `kernel/fd.h` 与 `kernel/fd.c`，PCB 不再直接保存 inode/offset：

```c
task_file_t* fds[TASK_FD_MAX];
```

每个对象包含：

- 引用计数 `refs`
- 类型：console / regular file / pipe
- inode、共享偏移、读写权限、append 标志
- pipe 端点与共享环形缓冲区

引用关系：

```text
fork
  parent fd ─┐
             ├─→ open_file(refs=2, off)
  child fd  ─┘

close(parent fd)
  → refs=1

close(child fd)
  → refs=0 → 释放对象
```

因此 `fork` 后父子既共享文件偏移，也共享管道端点生命周期。进程退出时先关闭
全部 fd，写端关闭后读端能立即看到 EOF，不会等僵尸进程被收尸。

命令行测试 `fdpipe` 验证了：

- 子进程读取前 3 字节后，父进程从共享偏移继续读后 3 字节。
- 管道写端退出后，读端第二次 `read` 返回 0。
- `dup2(pipe_write, 1)` 后，`write(1, ...)` 进入管道。

## 2. Pipe

每个 pipe 内核对象使用 1KB 环形缓冲区：

```text
used < 1024: writer 可写
used > 0:    reader 可读
reader 退场: writer 收到 EPIPE 语义 (-1)
writer 退场且缓冲区为空: reader 得到 EOF (0)
缓冲区空/满: 任务以 TASK_WAIT_PIPE 睡眠
```

读写方修改状态后广播唤醒 pipe 等待者，醒来任务重新检查条件。所有队列状态修改
都在关闭中断的临界区内完成，避免 timer 抢占造成丢失唤醒。

当前支持单级管道，即两个命令。解析器预留了 `PIPE_MAX=2` 的结构，后续增加
多级管道时需要同时扩展进程等待和 fd 清理逻辑。

## 3. 新增系统调用

| 调用 | 作用 |
|---|---|
| `SYS_PIPE` | 创建共享读/写端 |
| `SYS_DUP` | 分配最低空闲 fd 并复制打开文件对象引用 |
| `SYS_DUP2` | 用指定 fd 覆盖标准输入/输出 |
| `SYS_REMOVE` | 用户态 `rm` |
| `SYS_READDIR` | 返回目录项名字、inode、类型和大小 |

`SYS_WAITPID` 的第三参数新增 `WNOHANG`。返回值为 0 表示子进程仍在运行，
-1 表示没有匹配的直接子进程；用户 Shell 用它非阻塞回收后台僵尸。

## 4. 多调用用户工具

为控制 180KB 内核镜像体积，没有为每个工具嵌入独立镜像，而是编译一个
`user/sh.c`，根据 `argv[0]` 选择行为：

```text
/bin/sh
/bin/echo
/bin/cat
/bin/ls
/bin/pwd
/bin/mkdir
/bin/rm
```

`exec_install_builtins()` 把同一份 flat 镜像发布成这些文件。历史键盘回显演示
仍可通过内核命令 `run echo` 使用，其文件名为 `/bin/echodemo`，避免与
标准 `echo` 工具冲突。

`sh.c` 支持：

- 提示符和逐字节退格编辑
- `/bin` 路径查找
- `fork/execve/waitpid`
- 内建 `cd`、`exit`、`help`
- `< input`、`> output`、`>> output`
- `cmd1 | cmd2`
- 尾随 `&` 后台执行

示例：

```text
sh:/> mkdir /work
sh:/> cd /work
sh:/work> echo hello > note.txt
sh:/work> echo second >> note.txt
sh:/work> cat < note.txt | cat
hello
second
sh:/work> echo done > /bg.txt &
sh:/work> cat /bg.txt
done
```

## 5. 原子性约束

`pipe()` 需要一次返回两个 fd。实现先占用第一个槽位，再寻找第二个槽位；
两次查找期间系统调用门关中断，因此不会被调度器插入另一个 `pipe/open`。
如果创建对象或写回用户数组失败，已分配端点会立即关闭并释放。

`dup2(old, new)` 的顺序是：

```text
retain(new object)
替换 fd[new]
release(old object)
```

先 retain 保证 `oldfd == newfd` 或复杂别名关系下对象不会提前析构。

## 6. 验证

新增两套测试：

```bash
python3 tools/test-fd.py
python3 tools/test-shell.py
```

`test-fd` 共 42 项，新增共享偏移、pipe、EOF、`dup2` 检查。
`test-shell` 共 29 项，覆盖启动、PATH 查找、基础工具、CWD、覆盖/追加重定向、
输入重定向、管道、后台任务、未知命令和语法错误。

完整回归：

```bash
make test-suite
```

18 套测试、文档统计 505 项全部通过；最终 `kernel.bin` 为 175036 字节，
低于引导加载器的 180KB 上限。

## 7. 当前限制

- 只支持一层管道，不支持 `a | b | c`。
- 没有引号、转义、变量展开、通配符和 `;` 命令分隔。
- 重定向主要按命令结构保存为输入/输出目标，不保留任意出现顺序的完整语义。
- 尚无信号、进程组、终端控制、前台进程控制和作业表。
- fd 上限仍为 8，pipe 缓冲区为 1KB。
- 用户工具通过复制同一 flat 镜像发布，尚未实现 inode 硬链接。
- 内核 Shell 仍是默认交互入口，用户态 Shell 需要 `run sh` 启动。

## 8. 下一步建议

里程碑 21 可以继续做：

1. 多级管道和动态命令数组。
2. 进程组、前台终端归属、Ctrl+C 信号和作业控制。
3. POSIX 风格 `[pid]` 作业表、`jobs/fg/bg`。
4. 为 `/bin` 工具增加 inode 硬链接，减少重复文件数据块。
5. 在稳定后把用户态 Shell 设为默认登录 Shell，内核 Shell 保留为恢复模式。
