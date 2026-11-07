# 下次开发交接

更新时间：2026-11-07

## 当前稳定状态

- 项目目录：`/home/mfw/Documents/claude001/NewOS`
- 分支：`main`
- 当前进度：里程碑 19
- 完整回归：在原有测试基础上新增 fd/CWD 套件
- 最终内核镜像低于当前 180KB 引导上限

## 今日完成

1. 里程碑 13：独立虚拟地址空间、按需分页、fork COW、guard page、
   `brk/mmap`。
2. 里程碑 14：`/bin` 发布、flat 文件加载、事务式 `exec`、PID 保持。
3. 里程碑 15：用户态 `waitpid`、参数栈、ELF32 和 `execve`。
4. 里程碑 16–18：用户 allocator、编辑器增强、马里奥增强。
5. 里程碑 19：进程级 fd 表、`open/read/write/close/lseek`、
   `chdir/getcwd/mkdir`、fd 0/1/2 和每进程 CWD。

关键文档：

- `docs/MILESTONE_13_VM.md`
- `docs/MILESTONE_14_EXEC.md`
- `docs/MILESTONE_15_PROCESS_ELF.md`
- `note/进程模型与ELF开发记录.md`

## 下次继续：里程碑 20

建议主题：**用户态 Shell 与管道/重定向基础**

按以下顺序完成：

1. 实现最小用户态 `sh`：提示符、命令分词、PATH 查找和 `fork/execve/waitpid`。
2. 提供用户态 `echo`、`cat`、`ls`、`pwd`、`mkdir`、`rm` 基础工具。
3. 增加 `pipe` 和 `dup/dup2`，把 fd 表升级为可共享的内核打开文件对象。
4. 实现 `<`、`>`、`>>` 和管道 `|`。
5. 保留内核 Shell 作为救援/调试入口，稳定后再决定是否默认切换到用户态 Shell。

建议先只支持：

```text
cmd arg...
cmd1 | cmd2
cmd < input > output
cmd &
```

## 当前已知限制

- 不支持动态链接、PT_INTERP、ET_DYN/PIE。
- 没有信号和 `WNOHANG`。
- argv 上限 16，envp 上限 8，参数文本上限 3KB。
- 文件系统为 1MB 内存 FS，单文件最大 48KB。
- 默认 Shell 仍在内核态；fd/CWD 已支持每进程。
- 尚无管道、重定向、`dup/dup2`、进程组和作业控制。
- fd 上限 8，fork 后父子文件偏移暂不共享。

## 下次开始前检查

```bash
cd /home/mfw/Documents/claude001/NewOS
git status --short --branch
make -j4
python3 tools/test-fd.py
```
