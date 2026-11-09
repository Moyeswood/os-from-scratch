# 下次开发交接

更新时间：2026-11-09

## 当前稳定状态

- 项目目录：`/home/mfw/Documents/claude001/NewOS`
- 分支：`main`
- 当前进度：里程碑 28（Shell 3.0）完成
- 回归：Shell 146 项、用户态键盘、fd 与默认 Ring 3 启动通过
- 最终内核镜像 258212 字节，低于 448KB（458752 字节）加载上限

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
12. 里程碑 26a：用户态 sigaction/sigprocmask/sigreturn、pending/屏蔽位图、
    handler 用户栈帧和裸汇编返回跳板。
13. 里程碑 26b：正式镜像默认进入 Ring 3 Shell，启动扇区模式字节提供
    内核恢复 Shell/测试镜像，用户 Shell 退出后自动回退。
14. 里程碑 27a：Shell 引号/转义/变量/分号、stderr 重定向、`$?`、
    阻塞读取 EINTR，以及 command-not-found 作业状态误判修复。
15. 里程碑 27b：`*`/`?` 通配符、字段拆分、Tab 补全、
    持久化历史、上下键导航与 `SA_RESTART` 自动重启。
16. 里程碑 28：左右/Home/End/Delete、Ctrl+R 历史搜索、草稿恢复、
    可变环境、`$()` 命令替换、多行输入、脚本与 `sh -c`。

关键文档：

- `docs/SESSION_2026-11-08_HANDOFF.md`（今日任务与明日起点）
- `docs/SESSION_2026-11-09_HANDOFF.md`（27b 完成情况）
- `docs/MILESTONE_28_SHELL_V3.md`（Shell 3.0 设计记录）
- `docs/MILESTONE_13_VM.md`
- `docs/MILESTONE_14_EXEC.md`
- `docs/MILESTONE_15_PROCESS_ELF.md`
- `docs/MILESTONE_20_USER_SHELL.md`
- `docs/MILESTONE_21_JOBS.md`
- `docs/MILESTONE_22_STOP_RESUME.md`
- `docs/MILESTONE_23_DISK_FS.md`
- `docs/MILESTONE_24_FS_V2.md`
- `docs/MILESTONE_25_CRASH_CONSISTENCY.md`
- `docs/MILESTONE_26_SIGNALS.md`
- `docs/MILESTONE_27_SHELL_V2.md`
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

## 已完成：里程碑 26a（用户态信号）

主题：**Ring 3 信号处理函数**

已完成 pending/mask、handler 调用、自动屏蔽当前信号、sigreturn
现场恢复，以及 Ctrl+C/Ctrl+Z 回归。

## 已完成：里程碑 26b（默认 Ring 3 Shell）

正式镜像 `build/os.img` 直接启动 `sh:/>`；测试/恢复镜像
`build/os-kernel.img` 清除启动模式字节，直接进入 `OS:/>`。
用户 Shell 正常或异常退出都会回到内核恢复 Shell。

## 已完成：里程碑 27a（Shell 2.0 基础）

已完成引号、转义、环境变量、`$?`、`;`、`2>`、`2>&1` 和
键盘/pipe 阻塞读取的 EINTR 返回。

## 已完成：里程碑 27b（交互式 Shell）

- `*`/`?` 通配符按目录展开并排序，引用或转义时保持字面值。
- 未引用变量值执行字段拆分；无匹配模式保留原文。
- Tab 补全内置命令、`/bin`、当前路径和带目录前缀的路径。
- 历史写入 `/.sh_history`，上下键回忆，`history [N]` 查看。
- `signal_flags(..., SA_RESTART)` 支持键盘/pipe 阻塞读写和
  `waitpid` 在 handler 返回后自动恢复。
- 修复 flat 用户镜像 `.bss` 尾部未映射的问题。

## 已完成：里程碑 28（Shell 3.0）

- 行内光标编辑、Home/End/Delete、历史草稿恢复与 Ctrl+R 搜索。
- 16 项可变环境表，支持局部赋值、export、unset、set、env 和临时环境。
- `$()` 命令替换支持嵌套、多行输出和单引号抑制。
- 未闭合引号/`$()`/尾部反斜杠的多行输入。
- `sh FILE` 脚本执行与 `sh -c CMD`。

## 下次建议：里程碑 29（Shell 稳定性与错误码）

1. 统一 POSIX 错误码，减少所有失败都返回 `-1`。
2. 修正赋值右侧字段拆分与命令替换非零状态的 `$?`。
3. 脚本支持续行、跨行引号和退出状态传播。
4. 为 glob/Tab/历史/环境增加独立边界压力测试。
5. 评估命令替换 1KB 上限，改为可回收用户堆缓冲。

## 当前已知限制

- 不支持动态链接、PT_INTERP、ET_DYN/PIE。
- 用户态信号已有 pending/mask/handler/sigreturn；键盘/pipe/waitpid
  支持 `SA_RESTART`，文件系统锁等待不返回 EINTR。
- Shell 已支持行内编辑、Ctrl+R、可变环境、命令替换、多行和脚本；
  错误码、赋值字段语义和脚本退出状态仍需统一。
- argv 上限 16，envp 上限 8，参数文本上限 3KB。
- 文件系统为 1MB 持久化 FS；结构上已支持一级间接块，实际单文件上限
  受磁盘容量约束。
- ATA 只支持 primary master、polling PIO 和 28-bit LBA。
- 日志只保护元数据；断电可能丢失尚未回写的文件数据，但不会留下不完整命名空间。
- 没有时间戳、权限位和二级间接块。
- 默认 Shell 已运行在 Ring 3；内核 Shell 保留为恢复入口。
- 最多四段管道；已有进程组、前台 Ctrl+C 和基础作业表。
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
