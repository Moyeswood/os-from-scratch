# 里程碑 14：文件系统可执行文件与 exec

日期：2026-11-06

## 结果

启动时把 12 个内嵌用户程序发布到 `/bin`。`run /bin/hello` 与内嵌
`run hello` 走同一套进程创建流程，但前者从内存文件系统读取镜像。

新增 `SYS_EXEC`：

```c
int exec(const char* path);
```

执行成功不返回，保留 PID，切换页目录并进入新镜像；任何加载或分配失败
都返回 `-1`，旧地址空间继续运行。

## 演示

```text
run /bin/hello   # 从文件加载
run execdemo     # 把当前进程替换为 /bin/hello
run execbad      # 缺文件/坏指针，旧镜像继续运行
```

## 实现要点

1. `fs_size()` 先取得文件大小，再按精确大小 `kmalloc`。
2. 新地址空间完全建立后才切换 CR3，之后释放旧页目录。
3. 覆盖当前 int 0x80 帧，直接 iret 到 `VM_IMAGE_BASE`。
4. 用户路径逐页验证后复制到内核栈。
5. VM 临界区改为保存并恢复 IF，避免 syscall 中途被无条件开中断。

## 验证

- 新套件 `tools/test-exec.py` 26 项，ALL PASS。
- Ring 3 回归 36 项，ALL PASS。
- 文件系统回归 43 项，ALL PASS。
- 新套件最终纳入完整回归。

完整教程见 `docs/MILESTONE_14_EXEC.md`。

## 下一步

- 解析 ELF32 program headers；
- `exec(path, argv, envp)` 初始栈；
- `open/read/write/close` 文件系统 syscall；
- 用户态 Shell、管道和重定向。
