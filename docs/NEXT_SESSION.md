# 下次开发交接

更新时间：2026-11-07

## 当前稳定状态

- 项目目录：`/home/mfw/Documents/claude001/NewOS`
- 分支：`main`
- 当前进度：里程碑 21
- 完整回归：18 套测试、文档统计 523 项全部通过
- 最终内核镜像 178908 字节，低于当前 180KB 引导上限

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

关键文档：

- `docs/MILESTONE_13_VM.md`
- `docs/MILESTONE_14_EXEC.md`
- `docs/MILESTONE_15_PROCESS_ELF.md`
- `docs/MILESTONE_20_USER_SHELL.md`
- `docs/MILESTONE_21_JOBS.md`
- `note/进程模型与ELF开发记录.md`

## 下次继续：里程碑 22

建议主题：**暂停/恢复与最小信号处理**

按以下顺序完成：

1. 增加 `TASK_STOPPED` 或可恢复的停止状态。
2. 实现 `SIGSTOP/SIGCONT` 和 Ctrl+Z。
3. 扩展 `waitpid` 的 `WUNTRACED` 语义。
4. 加入 `bg %n`，支持暂停作业后台恢复。
5. 实现最小用户态信号处理回调与返回现场。
6. 为 `/bin` 多调用工具增加硬链接，降低重复数据块。

建议先只支持：

```text
cmd1 | cmd2 | cmd3 | cmd4
Ctrl+C → 当前前台进程组
Ctrl+Z → 暂停当前作业
jobs / fg %n / bg %n
```

## 当前已知限制

- 不支持动态链接、PT_INTERP、ET_DYN/PIE。
- 信号仅支持默认终止，尚无处理函数、暂停和恢复。
- argv 上限 16，envp 上限 8，参数文本上限 3KB。
- 文件系统为 1MB 内存 FS，单文件最大 48KB。
- 默认 Shell 仍在内核态；fd/CWD 已支持每进程。
- 最多四段管道；已有进程组、前台 Ctrl+C 和基础作业表。
- 信号只有默认终止；尚无 Ctrl+Z、`SIGSTOP/SIGCONT`、`bg` 或用户处理函数。
- 无引号、转义、变量展开、通配符和 `;`。
- fd 上限 16，pipe 缓冲区 1KB；fork 后文件偏移和 pipe 端点已共享。

## 下次开始前检查

```bash
cd /home/mfw/Documents/claude001/NewOS
git status --short --branch
make -j4
python3 tools/test-fd.py
python3 tools/test-shell.py
```
