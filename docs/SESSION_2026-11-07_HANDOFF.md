# 2026-11-07 会话交接

## 远端稳定点

- 仓库：`Moyeswood/os-from-scratch`
- 分支：`main`
- 里程碑 22 代码提交：`a459ac1 feat(jobs): 里程碑 22 — 暂停恢复与 Ctrl+Z 作业控制`
- 本交接文档随后单独提交
- 完成交接提交后，本地 `HEAD`、`origin/main` 一致且工作区干净

## 今天完成的里程碑

| 里程碑 | 提交 | 内容 |
|---|---|---|
| 20 | `1f3397a` | 共享打开文件对象、pipe/dup2、用户态 Shell、重定向和基础工具 |
| 21 | `0dbaecd` | 四段管道、进程组、Ctrl+C、jobs/fg/kill/wait |
| 22 | `a459ac1` | TASK_STOPPED、SIGSTOP/SIGCONT、Ctrl+Z、WUNTRACED、bg/fg 恢复 |

额外修复：

- 内核 Makefile 现在依赖全部 `kernel/*.h`，避免结构体布局变化后留下旧目标文件。
- `test-shell.py` 的中文串口输出改用字节切片后再解码，修复 Unicode 索引错位导致的假失败。
- 作业编号使用空闲槽位 `%1..%4`，彻底结束和回收后编号可复用。

## 当前可演示功能

```text
run sh
echo hello | cat | cat | cat
mkdir /work
echo data > /work/a.txt
spinner &
jobs
fg %1
Ctrl+Z
jobs
bg %1
fg %1
Ctrl+C
```

## 验证状态

- `make test-suite`：18 套测试全部通过
- 文档统计：535 项
- `test-shell.py`：59 项
- `kernel.bin`：180892 字节
- 引导上限：184320 字节（360 扇区）
- 剩余空间：3428 字节

## 明天从这里继续

目标：**里程碑 23 — 持久化磁盘文件系统**

建议第一步先不要直接改现有内存 FS。按以下顺序推进：

1. 运行 `git pull --ff-only`、`make -j4`、`python3 tools/test-shell.py`，
   确认从 `a459ac1` 起步。
2. 检查 ATA 主/从盘识别和 `0x1F0` I/O 端口访问。
3. 新建 `kernel/ata.c/h`，先实现：
   - `ata_init()`
   - `ata_read_sector(lba, buf)`
   - `ata_write_sector(lba, buf)`
4. 用 QEMU 临时镜像做读写回验，不启用 `-snapshot`；先不要格式化用户数据。
5. 读路径验证稳定后，再把现有 superblock/inode/direct-block 格式接到磁盘。
6. 最后实现 mount 检查、最小 fsck 和重启持久化测试。

## 明天先处理的容量问题

当前内核只剩 3428 字节引导空间，ATA 驱动和持久化校验代码很可能使镜像超限。
在写大量代码前先决定容量方案：

- 优先评估提高 `boot/boot.asm` 的扇区读取数量（镜像本身有 1.44MB 空间）。
- 若仍不够，再精简内嵌用户测试程序或把测试程序改为按需从镜像读取。
- 每次新增模块后立即检查 `build/kernel.bin` 大小，不要等到功能完成后才发现超限。

## 已知限制

- 信号没有用户态处理函数和待处理信号队列。
- 暂停期间不保留时间睡眠的剩余 deadline。
- 文件系统仍是内存 FS，重启后数据丢失。
- 只有单文件系统实例，无设备抽象、挂载表和块缓存。
- 最多四段管道、16 个 fd、4 个用户态作业槽。

## 下次开始命令

```bash
cd /home/mfw/Documents/claude001/NewOS
git status --short --branch
git log -3 --oneline
make -j4
python3 tools/test-shell.py
```
