# 下次开发交接

更新时间：2026-11-06

## 当前稳定状态

- 项目目录：`/home/mfw/Documents/claude001/newos-milestone13`
- 分支：`main`
- 稳定提交：`f99cf96 feat(proc): 里程碑 15 — waitpid、参数栈与 ELF32 加载`
- 工作区：干净，与 `origin/main` 同步
- 完整回归：14 套，386 项，ALL PASS
- 最终内核镜像：141,284 字节，低于 150 KiB 引导上限

## 今日完成

1. 里程碑 13：独立虚拟地址空间、按需分页、fork COW、guard page、
   `brk/mmap`。
2. 里程碑 14：`/bin` 发布、flat 文件加载、事务式 `exec`、PID 保持。
3. 里程碑 15：用户态 `waitpid`、detached 后台任务、精确僵尸回收、
   `argc/argv/envp` 初始栈、ELF32 PT_LOAD/BSS、只读代码页、
   `execve(path, argv, envp)`。

关键文档：

- `docs/MILESTONE_13_VM.md`
- `docs/MILESTONE_14_EXEC.md`
- `docs/MILESTONE_15_PROCESS_ELF.md`
- `note/进程模型与ELF开发记录.md`

## 下次继续：里程碑 16

建议主题：**文件描述符系统调用与用户态 Shell 基础**

按以下顺序完成：

1. 在 PCB 中增加小型文件描述符表。
2. 实现 `open/read/write/close/lseek`。
3. 将当前文件系统 inode API 包装为进程级 fd；加入偏移和生命周期检查。
4. 增加用户库包装与 `filedemo`，验证 stdin/stdout/file fd。
5. 让用户程序通过 fd 读取已经发布到 `/bin` 的文件。
6. 稳定后开始用户态 Shell。
7. 再实现管道和重定向。

建议先只支持：

```text
O_RDONLY / O_WRONLY / O_CREATE / O_TRUNC
fd 0 = stdin
fd 1 = stdout
fd 2 = stderr (先复用 output)
```

## 当前已知限制

- 不支持动态链接、PT_INTERP、ET_DYN/PIE。
- 没有信号和 `WNOHANG`。
- argv 上限 16，envp 上限 8，参数文本上限 3KB。
- 文件系统为 1MB 内存 FS，单文件最大 48KB。
- Shell 仍在内核态。
- 尚无管道、重定向、进程组和工作目录 per-process。

## 下次开始前检查

```bash
cd /home/mfw/Documents/claude001/newos-milestone13
git status --short --branch
make -j4
make test-process
```

从干净、已推送的 `f99cf96` 检查点继续。
