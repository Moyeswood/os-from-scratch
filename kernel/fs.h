#ifndef FS_H
#define FS_H

#include "types.h"

// ============================================================
//  内存文件系统 (NewOS 第一个文件系统, 教学向)
//
//  磁盘结构 (xv6 风格简化版):
//    块 = 页 = 4096 字节; 整个 FS 1MB = 256 块, 开机时
//    用 alloc_page 分配并格式化 (内存 FS, 重启即重置)。
//
//    块 0: 超级块 (magic + inode 位图 + 块位图 + 统计)
//    块 1: inode 表 (64 个 inode × 64 字节, 正好一页)
//    块 2: 根目录数据
//    块 3..255: 空闲数据块
//
//  ★inode 0 永久保留不分配 (目录项用 inum=0 表示空槽, 经典 xv6
//  做法), 根目录 = inode 1, 根的 "." 和 ".." 都指向自己。
//
//  每个文件/目录 = 一个 inode, 数据在 direct[12] 个块里
//  (文件最大 12×4KB = 48KB)。目录数据 = 一串 32 字节目录项
//  (inode 号 + 名字), 每块 128 项; "." 和 ".." 是真实目录项。
// ============================================================

#define FS_DIR   2
#define FS_FILE  1
#define FS_PATH_MAX 256   // 路径字符串上限

// 初始化/格式化并挂载 (开机时调用一次; 含自测, 串口报结果)
void fs_init(void);

// 路径查找 → inode 号 (不存在或非法路径 → -1)
int fs_lookup(const char* path);

// 创建文件 (FS_FILE) 或目录 (FS_DIR) → inode 号, 失败 -1
int fs_create(const char* path, uint32_t type);

// 删除文件或空目录 → 0 / -1
int fs_remove(const char* path);

// 按 inode 读写: 读从 off 开始最多 n 字节 (返回实际字节数)
// 写: append=0 先截断再写, append=1 追加; 返回写入字节数或 -1
int fs_read(uint32_t inum, void* buf, uint32_t off, uint32_t n);
int fs_write(uint32_t inum, const void* buf, uint32_t n, int append);

// 目录迭代: 第 idx 个目录项 → 名字/inode/类型/大小 (0=成功, 0 项=结束)
int fs_readdir(uint32_t inum, uint32_t idx,
               char* name, uint32_t* entry_inum, uint32_t* type, uint32_t* size);

// 当前目录
int fs_chdir(const char* path);
void fs_pwd(char* out);              // 把 cwd 的绝对路径写入 out

// 统计: 剩余块/剩余 inode/总块数/总 inode 数
void fs_stat(uint32_t* free_blocks, uint32_t* free_inodes,
             uint32_t* total_blocks, uint32_t* total_inodes);

#endif
