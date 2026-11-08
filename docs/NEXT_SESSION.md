# 下次开发交接

更新时间：2026-11-08

## 当前稳定状态

- 项目目录：`/home/mfw/Documents/claude001/NewOS`
- 分支：`main`
- 当前进度：里程碑 25
- 回归：文件系统、用户态和磁盘持久化套件通过；全量回归待下一轮统一执行
- 最终内核镜像 220068 字节，低于 448KB（458752 字节）加载上限

## 今日完成

1. 里程碑 13：独立虚拟地址空间、按需分页、fork COW、guard page、
   `brk/mmap`。
2. 里程碑 14：`/bin` 发布、flat 文件加载、事务式 `exec`、PID 保持。
3. 里程碑 15：用户态 `waitpid`、参数栈、ELF32 和 `execve`。
4. 里程碑 16–18：用户 allocator、编辑器增强、马里奥增强。
5. 里程碑 19：进程级 fd 表、`open/read/write/close/lseek`、
   `chdir/getcwd/mkdir`、fd 0/1/2 和每进程 CWD。
6. 里程碑 20：共享打开文件对象、`pipe/dup/dup2`、`WNOHANG`、
   用户态 Shell 和基础工具、`<`/`>`/`>>`/`|`/`&`。
7. 里程碑 21：四段管道、进程组、控制台前台组、Ctrl+C、
   `jobs/fg/kill/wait` 和后台作业回收。
8. 里程碑 22：`TASK_STOPPED`、SIGSTOP/SIGCONT、Ctrl+Z、
   `waitpid(WUNTRACED)`、`bg` 和暂停作业恢复。
9. 里程碑 23：ATA PIO、独立 2MB 磁盘、LBA 2048 文件系统布局、
   挂载检查、写穿式 dirty 块缓存和跨重启持久化。
10. 里程碑 24：一级间接块、文件系统全局临界区、`rename/stat`、
    用户态 `mv`、`stat` 和 `ls -l`，以及双进程并发写压力测试。
11. 里程碑 25：可睡眠 FS mutex、有界延迟回写、周期 flush、
    元数据 redo journal、断电重放、rename 覆盖、硬链接和 truncate。

关键文档：

- `docs/SESSION_2026-11-07_HANDOFF.md`（今日任务与明日起点）
- `docs/MILESTONE_13_VM.md`
- `docs/MILESTONE_14_EXEC.md`
- `docs/MILESTONE_15_PROCESS_ELF.md`
- `docs/MILESTONE_20_USER_SHELL.md`
- `docs/MILESTONE_21_JOBS.md`
- `docs/MILESTONE_22_STOP_RESUME.md`
- `docs/MILESTONE_23_DISK_FS.md`
- `docs/MILESTONE_24_FS_V2.md`
- `docs/MILESTONE_25_CRASH_CONSISTENCY.md`
- `note/进程模型与ELF开发记录.md`

## 已完成：里程碑 25

主题：**延迟回写与崩溃一致性**

按以下顺序完成：

1. 把整段关中断的 FS 临界区改成可睡眠 mutex。
2. 从写穿改为可配置的延迟回写，并增加脏块数量上限和周期 flush。
3. 设计日志或写时复制，避免断电时留下半更新的目录/inode。
4. 增加磁盘断电/损坏注入测试，验证恢复与最小 fsck 诊断。
5. 增加 `rename` 覆盖目标、硬链接和更完整的截断语义。

建议先只支持：

```text
single-writer sleeping mutex
delayed dirty writeback
bounded dirty-block cache
crash-safe metadata update
```

验证入口：

```bash
make test-disk
make test-fs2
make test-fs-journal
```

## 下次继续：里程碑 26

建议主题：**默认 Ring 3 Shell 与完整信号**

1. 正常启动直接进入用户态 Shell，内核 Shell 保留为恢复/调试入口。
2. 增加用户态信号处理函数、信号屏蔽字和待处理队列。
3. 在中断/系统调用返回用户态前递送信号，构造用户栈上的 handler 返回帧。
4. 支持 `sigaction`、`sigprocmask` 和可恢复的 `SIGINT/SIGTERM`。
5. 确保 Ctrl+C、作业控制和信号处理函数不会破坏用户栈或调度状态。

## 当前已知限制

- 不支持动态链接、PT_INTERP、ET_DYN/PIE。
- 信号支持默认终止、暂停和恢复，尚无用户态处理函数与待处理队列。
- argv 上限 16，envp 上限 8，参数文本上限 3KB。
- 文件系统为 1MB 持久化 FS；结构上已支持一级间接块，实际单文件上限
  受磁盘容量约束。
- ATA 只支持 primary master、polling PIO 和 28-bit LBA。
- 日志只保护元数据；断电可能丢失尚未回写的文件数据，但不会留下不完整命名空间。
- 没有时间戳、权限位和二级间接块。
- 默认 Shell 仍在内核态；fd/CWD 已支持每进程。
- 最多四段管道；已有进程组、前台 Ctrl+C 和基础作业表。
- 信号只有默认终止/暂停/恢复；尚无用户态处理函数和待处理信号队列。
- 无引号、转义、变量展开、通配符和 `;`。
- fd 上限 16，pipe 缓冲区 1KB；暂停进程保留全部 VM 和 fd 资源。

## 下次开始前检查

```bash
cd /home/mfw/Documents/claude001/NewOS
git status --short --branch
make -j4
python3 tools/test-fd.py
python3 tools/test-shell.py
python3 tools/test-disk.py
python3 tools/test-fs2.py
python3 tools/test-fs-journal.py
```
