# 里程碑 23：持久化磁盘文件系统

## 目标

里程碑 22 的文件系统已有超级块、inode、位图、目录和直接块，但它们只存在于
`alloc_page()` 返回的匿名内存页中。本次工作把同一套格式落到 ATA 磁盘，并保证：

1. 文件、目录和删除状态跨重启保留。
2. 系统启动时检查磁盘格式，不无条件重新格式化。
3. 每次修改在返回 Shell 前写回磁盘。
4. 没有磁盘时仍可使用原来的内存文件系统，兼容既有测试和演示。

## 1. 扩大内核加载窗口

内核原上限是 180KB，引导扇区从软盘逐扇区读取 360 个 512 字节扇区。加入 ATA
驱动和持久化逻辑后，内核已经达到 185404 字节，不再有安全余量。

`boot/boot.asm` 现在读取 896 个扇区，即 448KB：

```text
kernel load: 0x10000 .. 0x80000
boot stack:  0x90000 向下增长
gap:         64KB
```

镜像本身仍为 1.44MB，加载窗口扩大后不需要压缩程序或裁剪功能。

## 2. ATA PIO 驱动

`kernel/ata.c/h` 访问传统 primary IDE 端口，支持主盘和 28-bit LBA：

```text
port 0x1F0 .. 0x1F7   data/error/command/status
port 0x3F6            alternate status/control
command 0xEC           IDENTIFY
command 0x20 / 0x30    READ / WRITE SECTOR
command 0xE7           FLUSH CACHE
```

驱动流程：

1. `ata_init()` 选择主盘、发送 IDENTIFY、等待 BSY 清除并读取 512 字节设备信息。
2. 检查 LBA 支持；主板没有硬盘或设备不是 ATA 盘时返回失败。
3. `ata_read_sector()`/`ata_write_sector()` 每次操作一个 512 字节扇区。
4. 每次 ATA 事务在关中断状态下完成，避免任务切换插入另一个端口事务。
5. 批量写入后由文件系统调用 `ata_flush()`，确保设备缓存写回稳定存储。

当前只实现 polling PIO，没有 ATA IRQ 或 DMA，足以支撑教学文件系统。

## 3. 磁盘布局

QEMU 使用独立磁盘 `build/disk.img`，大小为 2MB。文件系统从 LBA 2048
（偏移 1MB）开始，前 1MB 预留给后续分区表、启动信息或其他块设备用途。

| 逻辑块 | 磁盘 LBA | 内容 |
|---|---:|---|
| 0 | 2048..2055 | 超级块 |
| 1 | 2056..2063 | 64 个 inode，每个 64 字节 |
| 2 | 2064..2071 | 根目录数据 |
| 3..255 | 后续扇区 | 文件/目录直接块 |

每个逻辑块为 4KB，恰好对应 8 个 ATA 扇区：

```text
disk_lba = 2048 + logical_block * 8 + sector_in_block
```

超级块先用魔数 `NEOS` (`0x534F454E`) 判断是否是 NewOS 文件系统，然后检查
块数、inode 数、根 inode、保留位和所有直接块引用。

## 4. 挂载、格式化与一致性检查

`fs_init()` 的顺序如下：

```text
ATA init
  ├─ 无磁盘 → 格式化内存 FS
  └─ 有磁盘 → 读取 256 个逻辑块
       ├─ 魔数正确且结构有效 → mounted
       ├─ 可读但未格式化     → format disk + sync
       └─ 魔数正确但结构损坏 → 拒绝覆盖磁盘，挂载临时内存 FS
```

最小 fsck 检查：

- inode 位图和块位图必须保留块 0、块 1 和 inode 0。
- 每个已分配 inode 的类型、大小和直接块必须合法。
- 已分配块不能越界、落在保留块或重复引用。
- 每个块位图置位的块必须被某个 inode 引用。
- `free_blocks`/`free_inodes` 计数错误可以自动重算并修复。
- 结构损坏不会自动格式化，避免一次挂载检查抹掉用户数据。

## 5. 写穿式块缓存

内存中的 256 个物理页继续作为逻辑块缓存。`fs.c` 新增 256-bit dirty map：

- 分配/释放块时标记超级块和数据块。
- 修改 inode 时标记 inode 表。
- 目录增删项时标记目录数据块和 inode 表。
- 文件写入时标记目标数据块。
- 每个修改型公开 API 返回前调用 `fs_sync()`。

同步时只写 dirty 块，每块写 8 个扇区，最后执行一次 `FLUSH CACHE`。这比重启时
整体扫描并写回 1MB 简单，也保留了之后加入延迟回写、块缓存淘汰和日志的空间。

## 6. 幂等 `/bin` 安装

内存 FS 每次启动都会清空，因此原 `exec_install_builtins()` 可以直接 `create`。
磁盘 FS 重启后 `/bin` 仍然存在，必须改变安装语义：

- 路径不存在：创建并写入。
- 文件已存在且大小一致：保留现有内容。
- 文件已存在但大小不一致：覆盖更新。
- `/bin` 必须是目录，不能用文件替换。

这样即使内核更新了内嵌程序，下次启动也会替换大小变化的二进制；同时普通重启
不需要重新写入 20 多个程序。

## 7. 构建和运行

```bash
make
make run
```

`make run` 同时挂载：

```text
floppy: build/os.img
IDE 0:  build/disk.img (primary master)
```

`build/disk.img` 只在不存在时创建，重新构建 OS 镜像不会清空用户文件。
如需恢复全新磁盘，先退出 QEMU，再显式删除该文件并重新运行 `make run`。

## 8. 自动化验证

新增 `tools/test-disk.py`，共 19 项检查：

- ATA IDENTIFY、LBA28 支持。
- 空白磁盘格式化、超级块落盘。
- 新建目录、写文件、读回、删除。
- 关闭 QEMU 后重新启动同一磁盘。
- 检查挂载而不是重新格式化。
- 检查目录、文件内容、删除状态都持久化。
- 检查 `/bin` 安装器在已有文件时保持幂等。

运行：

```bash
make test-disk
```

该测试使用 `build/test-disk.img`，不会污染 `make run` 使用的
`build/disk.img`。

## 9. 当前限制

- ATA 只支持 primary master、PIO、28-bit LBA，没有 IRQ、DMA、分区表和多设备。
- 文件系统固定 1MB，只有一级直接块，单文件最大 48KB。
- 每次修改同步 dirty 块，尚无延迟回写、LRU 缓存和日志事务。
- 文件系统操作还没有独立全局锁；并发写同一路径仍不属于当前支持范围。
- 最小 fsck 检测数据结构，不恢复孤立文件或损坏目录。
- CWD 只保存在进程/任务对象中，重启后从根目录开始。

## 10. 下一步

建议里程碑 24 优先做块缓存与一致性：

1. 全局 FS 锁和打开文件截断语义。
2. 延迟脏块回写、块引用计数与 LRU 淘汰。
3. 间接块，突破 48KB 单文件上限。
4. `rename`、`link`、`unlink`、`stat`。
5. 日志或写时复制，保证目录更新和 inode 更新的事务性。
