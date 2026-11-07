# 下次开发交接

更新时间：2026-11-07

## 当前稳定状态

- 项目目录：`/home/mfw/Documents/claude001/NewOS`
- 分支：`main`
- 当前进度：里程碑 22
- 完整回归：18 套测试、文档统计 535 项全部通过
- 最终内核镜像 180892 字节，低于 180KB（184320 字节）引导上限

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

关键文档：

- `docs/SESSION_2026-11-07_HANDOFF.md`（今日任务与明日起点）
- `docs/MILESTONE_13_VM.md`
- `docs/MILESTONE_14_EXEC.md`
- `docs/MILESTONE_15_PROCESS_ELF.md`
- `docs/MILESTONE_20_USER_SHELL.md`
- `docs/MILESTONE_21_JOBS.md`
- `docs/MILESTONE_22_STOP_RESUME.md`
- `note/进程模型与ELF开发记录.md`

## 下次继续：里程碑 23

建议主题：**持久化磁盘文件系统**

按以下顺序完成：

1. 先检查引导容量：当前只剩 3428 字节，决定扩大读盘扇区数还是精简嵌入程序。
2. 实现 ATA PIO 扇区读写驱动，并用临时磁盘镜像验证。
3. 把现有 1MB 内存 FS 的超级块、inode 和目录格式落盘。
4. 增加挂载检查与最小一致性修复。
5. 验证断电重启后文件、目录和 CWD 对象仍可用。
6. 增加读缓存与脏块回写策略，再补充硬链接、`rename` 和 `stat`。

建议先只支持：

```text
ATA PIO read/write sector
mkfs / mount
create / write / close
reboot → mount → cat file
```

## 当前已知限制

- 不支持动态链接、PT_INTERP、ET_DYN/PIE。
- 信号支持默认终止、暂停和恢复，尚无用户态处理函数与待处理队列。
- argv 上限 16，envp 上限 8，参数文本上限 3KB。
- 文件系统为 1MB 内存 FS，单文件最大 48KB。
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
```
