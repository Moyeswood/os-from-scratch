# 里程碑 15：waitpid、参数栈与 ELF32

日期：2026-11-06

## 结果

用户态新增 `waitpid(pid, &status)`，支持指定直接子进程或 `-1` 等待任意
孩子。退出码只在父进程仍存活时保留；spawn 清扫不会偷走活父进程的
ZOMBIE。后台任务改为 `parent_pid=0` 的 detached 任务。

进程入口现在拥有按 cdecl 参数约定构造的初始栈：

```text
argc / argv / envp
```

加载器会自动识别 flat 与 ELF32。ELF32 支持 ET_EXEC、PT_LOAD、数据段、
NOBITS/BSS 和只读代码页。

## 新程序

```text
run waittest                 # 子进程状态与僵尸回收
run /bin/elfdemo alpha beta  # ELF 段、BSS、argv/envp
run execargv                # execve 传递自定义参数和环境
```

## 关键修复

1. 后台任务不能以永久存活的 Shell 作为父进程，否则永远不会成为孤儿。
2. waitpid 的睡眠循环必须在 cli 下同时重查 ZOMBIE 与孩子是否存在。
3. 初始栈的 `[ESP+0]` 是 fake return，参数从 `[ESP+4]` 开始，不能把
   argc 直接放在 ESP 所指向位置。
4. ELF BSS 必须按 `p_memsz` 而不是 `p_filesz` 映射。
5. 只读 ELF 页可以共享，但 fork 时不能错误地设置 COW 写权限。
6. 首次大 ELF 加载会让内核堆扩容，页数泄漏测试需要先预热内核堆路径。

## 验证

- 新套件 `tools/test-process.py` 39 项，ALL PASS。
- ELF 示例镜像包含两个 LOAD 段。
- `waitpid` 的 42/7 状态、非法指针和后台清扫窗口全部通过。
- `execve` 的 argv/envp 与 PID 保持通过。
- 完整回归最终为 14 套 386 项。

完整教程见 `docs/MILESTONE_15_PROCESS_ELF.md`。

## 下一步

- `open/read/write/close/lseek`；
- 用户态 Shell；
- 管道、重定向；
- 信号和 WNOHANG。
