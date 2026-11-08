#ifndef FS_H
#define FS_H

#include "types.h"

// ============================================================
//  持久化磁盘文件系统 (NewOS 第一个文件系统, 教学向)
//
//  磁盘结构 (xv6 风格简化版):
//    块 = 页 = 4096 字节; 整个 FS 1MB = 256 块。启动时从
//    ATA 主盘 LBA 2048 开始装入块缓存；每次修改写穿回磁盘。
//
//    块 0: 超级块 (magic + inode 位图 + 块位图 + 统计)
//    块 1: inode 表 (64 个 inode × 64 字节, 正好一页)
//    块 2: 根目录数据
//    块 3..255: 空闲数据块
//
//  ★inode 0 永久保留不分配 (目录项用 inum=0 表示空槽, 经典 xv6
//  做法), 根目录 = inode 1, 根的 "." 和 ".." 都指向自己。
//
//  每个文件/目录 = 一个 inode, 数据在 direct[12] 个块里；更大的
//  文件通过一级间接块继续扩展 (单文件上限受 1MB 文件系统容量限制)。
//  目录数据 = 一串 32 字节目录项 (inode 号 + 名字), 每块 128 项;
//  "." 和 ".." 是真实目录项。inode 末尾的原保留字段存放间接块号，
//  因此旧磁盘 (该字段为 0) 可以直接挂载。
// ============================================================

#define FS_DIR   2
#define FS_FILE  1
#define FS_PATH_MAX 256   // 路径字符串上限

typedef struct {
    uint32_t inum;
    uint32_t type;
    uint32_t size;
    uint32_t nlink;
} fs_attr_t;

// 初始化并挂载 ATA 磁盘 FS。无磁盘或磁盘损坏时提供易失回退；
// 开机时调用一次，含结构检查和自测，串口报结果。
void fs_init(void);

// 路径查找 → inode 号 (不存在或非法路径 → -1)
int fs_lookup(const char* path);

// 文件大小 (字节); 不存在、目录或非法路径 → -1
int fs_size(const char* path);

// 创建文件 (FS_FILE) 或目录 (FS_DIR) → inode 号, 失败 -1
int fs_create(const char* path, uint32_t type);

// 删除文件或空目录 → 0 / -1
int fs_remove(const char* path);

// 重命名文件或目录。目标存在时按 POSIX 风格原子替换：文件替换文件，
// 目录只能替换空目录；跨目录移动目录时同步更新其 ".." 目录项。
// 整个命名空间的元数据更新由一个 redo 日志事务保护。
int fs_rename(const char* oldpath, const char* newpath);

// 查询文件/目录元数据 → 0 / -1
int fs_getattr(const char* path, fs_attr_t* attr);

// 按 inode 读写: 读从 off 开始最多 n 字节 (返回实际字节数)
// 写: append=0 先截断再写, append=1 追加; 返回写入字节数或 -1
int fs_read(uint32_t inum, void* buf, uint32_t off, uint32_t n);
int fs_write(uint32_t inum, const void* buf, uint32_t n, int append);
int fs_write_at(uint32_t inum, const void* buf, uint32_t off, uint32_t n);
int fs_truncate(uint32_t inum);
int fs_truncate_size(uint32_t inum, uint32_t size);
int fs_inode_type(uint32_t inum);
int fs_inode_size(uint32_t inum);

// 目录迭代: 第 idx 个目录项 → 名字/inode/类型/大小 (0=成功, 0 项=结束)
int fs_readdir(uint32_t inum, uint32_t idx,
               char* name, uint32_t* entry_inum, uint32_t* type, uint32_t* size);

// 为普通文件创建硬链接 (目录不允许硬链接)。
int fs_link(const char* oldpath, const char* newpath);

// 当前目录
int fs_chdir(const char* path);
void fs_pwd(char* out);              // 把 cwd 的绝对路径写入 out

// 文件系统容量统计: 剩余块/剩余 inode/总块数/总 inode 数
void fs_stat(uint32_t* free_blocks, uint32_t* free_inodes,
             uint32_t* total_blocks, uint32_t* total_inodes);

// 强制提交脏数据与元数据日志; 普通文件 close/reboot 也会调用。
int fs_flush(void);

// 当前延迟回写缓存状态 (块数/其中元数据块数)。
void fs_cache_stats(uint32_t* dirty_blocks, uint32_t* dirty_metadata);

// QEMU 故障注入: 提交元数据日志后停在写 home 之前，重启时应自动重放。
void fs_crashpoint(void);

// 空闲任务周期调用: 非阻塞获取锁，到时间后提交脏缓存。
void fs_periodic_flush(void);

#endif
