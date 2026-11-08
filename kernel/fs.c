// ============================================================
//  持久化磁盘文件系统实现 (见 fs.h 的布局说明)
//
//  块与页的映射: fs_block[i] 存第 i 个逻辑块对应的物理地址
//  (alloc_page 分配, 不要求物理连续)。A 盘上的逻辑块从 LBA 2048
//  开始线性映射，dirty 块在修改型 API 返回前写穿回 ATA 磁盘。
// ============================================================

#include "fs.h"
#include "ata.h"
#include "string.h"
#include "memory.h"
#include "serial.h"
#include "task.h"
#include "types.h"

#define FS_NBLOCKS 256
#define FS_NINODES 64
#define FS_BSIZE   4096
#define FS_NDIRECT 12                    // inode 内的直接块数
#define FS_PTRS_PER_BLOCK (FS_BSIZE / sizeof(uint32_t))
#define FS_MAXFILE_BLOCKS (FS_NDIRECT + FS_PTRS_PER_BLOCK)
#define FS_MAXNAME 27                    // 目录项名字上限 (28 字节含 \0)
#define FS_PATH_MAX 256
#define FS_MAGIC   0x534F454E            // 内存字节序 = "NEOS"
#define FS_DISK_BASE_LBA 2048u           // 磁盘镜像前 1MB 留给其他用途
#define FS_BLOCK_SECTORS (FS_BSIZE / ATA_SECTOR_SIZE)

// 磁盘结构
struct fs_super {
    uint32_t magic;
    uint32_t nblocks;
    uint32_t ninodes;
    uint32_t root_inum;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint8_t  inode_bmap[FS_NINODES / 8];   // 8 字节, 64 bit
    uint8_t  block_bmap[FS_NBLOCKS / 8];   // 32 字节, 256 bit
};

struct fs_inode {                          // 64 字节 (64 个 inode 正好一页)
    uint32_t type;                         // 0=空闲 1=文件 2=目录
    uint32_t size;                         // 字节数
    uint32_t nlink;                        // 链接数 (目录 = 1 + 子目录数)
    uint32_t direct[FS_NDIRECT];
    uint32_t indirect;                     // 一级间接块; 旧格式中为 0
};

struct fs_dirent {                         // 32 字节, 每块 128 项
    uint32_t inum;                         // 0 = 空槽
    char name[FS_MAXNAME + 1];
};

// 逻辑块 → 物理地址
static uint8_t* fs_block[FS_NBLOCKS];
static struct fs_super* sb;                // = 块 0
static struct fs_inode* itab;              // = 块 1 (inode 表)
static uint8_t fs_dirty[FS_NBLOCKS / 8];
static int fs_disk_mounted;

static void fs_mark_dirty(uint32_t block) {
    if (block < FS_NBLOCKS)
        fs_dirty[block / 8] |= (uint8_t)(1u << (block % 8));
}

static int fs_block_dirty(uint32_t block) {
    return (fs_dirty[block / 8] >> (block % 8)) & 1;
}

static void fs_mark_clean(uint32_t block) {
    fs_dirty[block / 8] &= (uint8_t)~(1u << (block % 8));
}

static uint32_t fs_irq_save_disable(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void fs_irq_restore(uint32_t flags) {
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
}

// Single-CPU filesystem lock. Interrupts stay disabled for the complete
// public operation, so neither a timer-driven task switch nor a nested
// filesystem call can observe a half-updated bitmap, inode or directory.
static uint32_t fs_lock(void) {
    return fs_irq_save_disable();
}

static void fs_unlock(uint32_t flags) {
    fs_irq_restore(flags);
}

// Write-through cache: every mutating public operation leaves the cache
// clean. The cache gives the FS a block interface while ATA remains a
// sector interface.
static int fs_sync(void) {
    if (!fs_disk_mounted) return 0;
    // Keep mark-clean atomic with the block contents that were written.
    uint32_t flags = fs_irq_save_disable();
    int result = 0;
    for (uint32_t b = 0; b < FS_NBLOCKS; b++) {
        if (!fs_block_dirty(b)) continue;
        uint32_t base = FS_DISK_BASE_LBA + b * FS_BLOCK_SECTORS;
        int ok = 1;
        for (uint32_t s = 0; s < FS_BLOCK_SECTORS; s++) {
            if (ata_write_sector(base + s,
                                 fs_block[b] + s * ATA_SECTOR_SIZE) < 0) {
                ok = 0;
                break;
            }
        }
        if (!ok || ata_flush() < 0) {
            serial_puts("[FS] ERROR: disk write failed at block ");
            serial_put_hex(b);
            serial_puts("\n\r");
            result = -1;
            goto out;
        }
        fs_mark_clean(b);
    }
out:
    fs_irq_restore(flags);
    return result;
}

static uint32_t current_cwd(void) {
    task_t* t = task_current();
    return t && t->cwd_inum ? t->cwd_inum : sb->root_inum;
}

// ---------- 位图 ----------
static int bit_get(const uint8_t* bmap, uint32_t i) {
    return (bmap[i / 8] >> (i % 8)) & 1;
}
static void bit_set(uint8_t* bmap, uint32_t i) {
    bmap[i / 8] |= (uint8_t)(1 << (i % 8));
}
static void bit_clear(uint8_t* bmap, uint32_t i) {
    bmap[i / 8] &= (uint8_t)~(1 << (i % 8));
}
static int bit_find_zero(const uint8_t* bmap, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        if (!bit_get(bmap, i)) return (int)i;
    return -1;
}

// ---------- 块分配 ----------
static int balloc(void) {
    int b = bit_find_zero(sb->block_bmap, FS_NBLOCKS);
    if (b < 0) return -1;
    bit_set(sb->block_bmap, (uint32_t)b);
    k_memset(fs_block[b], 0, FS_BSIZE);
    sb->free_blocks--;
    fs_mark_dirty(0);
    fs_mark_dirty((uint32_t)b);
    return b;
}

static void bfree(uint32_t b) {
    bit_clear(sb->block_bmap, b);
    sb->free_blocks++;
    fs_mark_dirty(0);
}

// Resolve logical data block idx. With create=0 this is a read-only lookup;
// with create=1 missing indirect/data blocks are allocated and zeroed.
static uint32_t inode_block(struct fs_inode* ip, uint32_t idx, int create) {
    if (idx < FS_NDIRECT) {
        if (!ip->direct[idx] && create) {
            int b = balloc();
            if (b < 0) return 0;
            ip->direct[idx] = (uint32_t)b;
            fs_mark_dirty(1);
        }
        return ip->direct[idx];
    }
    if (idx >= FS_MAXFILE_BLOCKS) return 0;

    if (!ip->indirect) {
        if (!create) return 0;
        int table = balloc();
        if (table < 0) return 0;
        ip->indirect = (uint32_t)table;
        fs_mark_dirty(1);
    }

    uint32_t slot = idx - FS_NDIRECT;
    uint32_t* pointers = (uint32_t*)fs_block[ip->indirect];
    if (!pointers[slot] && create) {
        int b = balloc();
        if (b < 0) return 0;
        pointers[slot] = (uint32_t)b;
        fs_mark_dirty(ip->indirect);
    }
    return pointers[slot];
}

// ---------- inode 分配 ----------
static int ialloc(uint32_t type) {
    int i = bit_find_zero(sb->inode_bmap, FS_NINODES);
    if (i < 0) return -1;
    bit_set(sb->inode_bmap, (uint32_t)i);
    struct fs_inode* ip = &itab[i];
    ip->type = type;
    ip->size = 0;
    ip->nlink = 1;
    for (int j = 0; j < FS_NDIRECT; j++) ip->direct[j] = 0;
    ip->indirect = 0;
    sb->free_inodes--;
    fs_mark_dirty(0);
    fs_mark_dirty(1);
    return i;
}

static void ifree(uint32_t i) {
    bit_clear(sb->inode_bmap, i);
    itab[i].type = 0;
    sb->free_inodes++;
    fs_mark_dirty(0);
    fs_mark_dirty(1);
}

// 释放 inode 的全部数据块并把 size 清零
static void inode_truncate(uint32_t inum) {
    struct fs_inode* ip = &itab[inum];
    for (int j = 0; j < FS_NDIRECT; j++) {
        if (ip->direct[j]) {
            bfree(ip->direct[j]);
            ip->direct[j] = 0;
        }
    }
    if (ip->indirect) {
        uint32_t* pointers = (uint32_t*)fs_block[ip->indirect];
        for (uint32_t i = 0; i < FS_PTRS_PER_BLOCK; i++) {
            if (pointers[i]) bfree(pointers[i]);
        }
        bfree(ip->indirect);
        ip->indirect = 0;
    }
    ip->size = 0;
    fs_mark_dirty(1);
}

// ---------- 目录 ----------
// 在目录 dinum 里找名字 → inode 号, 不存在 -1
static int dir_lookup(uint32_t dinum, const char* name) {
    struct fs_inode* dp = &itab[dinum];
    if (dp->type != FS_DIR) return -1;
    uint32_t nent = dp->size / sizeof(struct fs_dirent);
    for (uint32_t i = 0; i < nent; i++) {
        uint32_t block = inode_block(dp, i / 128, 0);
        if (!block) return -1;
        struct fs_dirent* de = (struct fs_dirent*)fs_block[block] + (i % 128);
        if (de->inum && k_strcmp(de->name, name) == 0)
            return (int)de->inum;
    }
    return -1;
}

// 往目录 dinum 追加一个目录项 → 0 / -1
static int dir_add(uint32_t dinum, const char* name, uint32_t inum) {
    struct fs_inode* dp = &itab[dinum];
    if (k_strlen(name) > FS_MAXNAME) return -1;
    uint32_t idx = dp->size / sizeof(struct fs_dirent);
    if (idx >= FS_MAXFILE_BLOCKS * 128) return -1;
    uint32_t blkno = idx / 128;
    uint32_t block = inode_block(dp, blkno, 1);
    if (!block) return -1;
    struct fs_dirent* de = (struct fs_dirent*)fs_block[block] + (idx % 128);
    de->inum = inum;
    k_strcpy(de->name, name);
    dp->size += sizeof(struct fs_dirent);
    fs_mark_dirty(block);
    fs_mark_dirty(1);
    return 0;
}

// 从目录 dinum 移除名字 name 的目录项 (不释放 inode, 只断开链接) → 0 / -1
static int dir_remove(uint32_t dinum, const char* name) {
    struct fs_inode* dp = &itab[dinum];
    uint32_t nent = dp->size / sizeof(struct fs_dirent);
    for (uint32_t i = 0; i < nent; i++) {
        uint32_t block = inode_block(dp, i / 128, 0);
        if (!block) return -1;
        struct fs_dirent* de = (struct fs_dirent*)fs_block[block] + (i % 128);
        if (de->inum && k_strcmp(de->name, name) == 0) {
            de->inum = 0;                        // 空槽
            de->name[0] = '\0';
            fs_mark_dirty(block);
            return 0;
        }
    }
    return -1;
}

// Replace an existing entry's inode and/or name. Used by same-directory
// rename and when a moved directory needs a new ".." target.
static int dir_replace(uint32_t dinum, const char* name,
                       const char* newname, uint32_t newinum) {
    struct fs_inode* dp = &itab[dinum];
    uint32_t nent = dp->size / sizeof(struct fs_dirent);
    for (uint32_t i = 0; i < nent; i++) {
        uint32_t block = inode_block(dp, i / 128, 0);
        if (!block) return -1;
        struct fs_dirent* de = (struct fs_dirent*)fs_block[block] + (i % 128);
        if (de->inum && k_strcmp(de->name, name) == 0) {
            de->inum = newinum;
            if (newname) k_strcpy(de->name, newname);
            fs_mark_dirty(block);
            return 0;
        }
    }
    return -1;
}

// ---------- 路径解析 ----------
// 把 path 走到"最后一段的父目录": 返回父 inode, 最后一段名字写入 last。
// 支持绝对/相对路径、"."、".."、连续 '/'; 中间段必须存在且是目录。
static int namex(const char* path, char* last) {
    if (k_strlen(path) >= FS_PATH_MAX) return -1;
    char pathbuf[FS_PATH_MAX];
    k_strcpy(pathbuf, path);
    char* p = pathbuf;
    uint32_t cur = current_cwd();
    if (*p == '/') { cur = sb->root_inum; p++; }

    for (;;) {
        while (*p == '/') p++;
        if (*p == '\0') { k_strcpy(last, "."); return (int)cur; }  // 尾 '/' 或空路径
        char* seg = p;
        while (*p && *p != '/') p++;
        uint32_t seglen = (uint32_t)(p - seg);
        if (seglen > FS_MAXNAME) return -1;
        for (uint32_t i = 0; i < seglen; i++) last[i] = seg[i];
        last[seglen] = '\0';

        if (*p == '\0') return (int)cur;        // 最后一段
        // 中间段: 必须存在且是目录
        int next = dir_lookup(cur, last);
        if (next < 0) return -1;
        if (itab[next].type != FS_DIR) return -1;
        cur = (uint32_t)next;
    }
}

// ---------- 公开 API ----------
int fs_lookup(const char* path) {
    uint32_t flags = fs_lock();
    char last[FS_MAXNAME + 1];
    int parent = namex(path, last);
    int result = -1;
    if (parent >= 0) {
        result = k_strcmp(last, ".") == 0 ? parent
                 : dir_lookup((uint32_t)parent, last);
    }
    fs_unlock(flags);
    return result;
}

int fs_size(const char* path) {
    uint32_t flags = fs_lock();
    int inum = fs_lookup(path);
    int result = inum < 0 || itab[inum].type != FS_FILE
                 ? -1 : (int)itab[inum].size;
    fs_unlock(flags);
    return result;
}

int fs_create(const char* path, uint32_t type) {
    uint32_t flags = fs_lock();
    char last[FS_MAXNAME + 1];
    int parent = namex(path, last);
    if (parent < 0 || (type != FS_FILE && type != FS_DIR) ||
        k_strcmp(last, ".") == 0 || k_strcmp(last, "..") == 0 ||
        dir_lookup((uint32_t)parent, last) >= 0 ||
        itab[parent].type != FS_DIR) {
        fs_unlock(flags);
        return -1;
    }

    int inum = ialloc(type);
    if (inum < 0) {
        fs_unlock(flags);
        return -1;
    }
    if (dir_add((uint32_t)parent, last, (uint32_t)inum) < 0) {
        ifree((uint32_t)inum);
        fs_unlock(flags);
        return -1;
    }
    if (type == FS_DIR) {
        // "." 和 ".." 是真实目录项 (ls 会隐藏它们, 但路径解析靠它们工作)
        dir_add((uint32_t)inum, ".", (uint32_t)inum);
        dir_add((uint32_t)inum, "..", (uint32_t)parent);
        itab[parent].nlink++;                      // 父目录多了个子目录
    }
    fs_mark_dirty(1);
    fs_sync();
    fs_unlock(flags);
    return inum;
}

// 目录是否为空 (删除留下的 inum=0 空槽不算, "."/".." 不算)
static int dir_is_empty(uint32_t inum) {
    struct fs_inode* dp = &itab[inum];
    uint32_t nent = dp->size / sizeof(struct fs_dirent);
    for (uint32_t i = 0; i < nent; i++) {
        uint32_t block = inode_block(dp, i / 128, 0);
        if (!block) return 0;
        struct fs_dirent* de = (struct fs_dirent*)fs_block[block] + (i % 128);
        if (de->inum && k_strcmp(de->name, ".") != 0 && k_strcmp(de->name, "..") != 0)
            return 0;
    }
    return 1;
}

// 目录 inum 是否被任何任务作为 cwd 或其祖先。
static int is_task_cwd_ancestor(uint32_t inum) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_t* t = task_at(i);
        if (!t || !t->cwd_inum) continue;
        uint32_t cur = t->cwd_inum;
        while (cur != sb->root_inum) {
            if (cur == inum) return 1;
            int up = dir_lookup(cur, "..");
            if (up < 0) break;
            cur = (uint32_t)up;
        }
        if (inum == sb->root_inum) return 1;
    }
    return 0;
}

int fs_remove(const char* path) {
    uint32_t flags = fs_lock();
    char last[FS_MAXNAME + 1];
    int parent = namex(path, last);
    if (parent < 0) {
        fs_unlock(flags);
        return -1;
    }
    int inum = dir_lookup((uint32_t)parent, last);
    if (inum < 0 || k_strcmp(last, ".") == 0 || k_strcmp(last, "..") == 0) {
        fs_unlock(flags);
        return -1;
    }
    struct fs_inode* ip = &itab[inum];

    if (ip->type == FS_DIR) {
        if (inum == (int)sb->root_inum ||
            is_task_cwd_ancestor((uint32_t)inum) ||
            !dir_is_empty((uint32_t)inum)) {
            fs_unlock(flags);
            return -1;
        }
        itab[parent].nlink--;
    }
    dir_remove((uint32_t)parent, last);
    ip->nlink--;
    if (ip->nlink == 0) {
        inode_truncate((uint32_t)inum);
        ifree((uint32_t)inum);
    }
    fs_mark_dirty(1);
    fs_sync();
    fs_unlock(flags);
    return 0;
}

// True when dinum is src or lies below it. Used to reject moving a directory
// into its own subtree, which would create an unreachable cycle.
static int dir_contains(uint32_t src, uint32_t dinum) {
    uint32_t cur = dinum;
    for (uint32_t depth = 0; depth < FS_NINODES; depth++) {
        if (cur == src) return 1;
        if (cur == sb->root_inum) return 0;
        int parent = dir_lookup(cur, "..");
        if (parent < 0 || (uint32_t)parent == cur) return 0;
        cur = (uint32_t)parent;
    }
    return 1;
}

int fs_rename(const char* oldpath, const char* newpath) {
    uint32_t flags = fs_lock();
    char oldname[FS_MAXNAME + 1];
    char newname[FS_MAXNAME + 1];
    int oldparent = namex(oldpath, oldname);
    int newparent = namex(newpath, newname);
    if (oldparent < 0 || newparent < 0 ||
        k_strcmp(oldname, ".") == 0 || k_strcmp(oldname, "..") == 0 ||
        k_strcmp(newname, ".") == 0 || k_strcmp(newname, "..") == 0) {
        fs_unlock(flags);
        return -1;
    }

    int inum = dir_lookup((uint32_t)oldparent, oldname);
    if (inum < 0 || itab[newparent].type != FS_DIR ||
        dir_lookup((uint32_t)newparent, newname) >= 0) {
        fs_unlock(flags);
        return -1;
    }
    if (oldparent == newparent && k_strcmp(oldname, newname) == 0) {
        fs_unlock(flags);
        return 0;
    }

    struct fs_inode* ip = &itab[inum];
    if (ip->type == FS_DIR) {
        if (inum == (int)sb->root_inum ||
            dir_contains((uint32_t)inum, (uint32_t)newparent)) {
            fs_unlock(flags);
            return -1;
        }
    }

    if (oldparent == newparent) {
        if (dir_replace((uint32_t)oldparent, oldname, newname,
                        (uint32_t)inum) < 0) {
            fs_unlock(flags);
            return -1;
        }
    } else {
        if (dir_add((uint32_t)newparent, newname, (uint32_t)inum) < 0) {
            fs_unlock(flags);
            return -1;
        }
        if (dir_remove((uint32_t)oldparent, oldname) < 0) {
            dir_remove((uint32_t)newparent, newname);
            fs_unlock(flags);
            return -1;
        }
        if (ip->type == FS_DIR) {
            if (dir_replace((uint32_t)inum, "..", 0,
                            (uint32_t)newparent) < 0) {
                fs_unlock(flags);
                return -1;
            }
            itab[oldparent].nlink--;
            itab[newparent].nlink++;
        }
    }

    fs_mark_dirty(1);
    fs_sync();
    fs_unlock(flags);
    return 0;
}

int fs_getattr(const char* path, fs_attr_t* attr) {
    if (!attr) return -1;
    uint32_t flags = fs_lock();
    int inum = fs_lookup(path);
    if (inum < 0) {
        fs_unlock(flags);
        return -1;
    }
    attr->inum = (uint32_t)inum;
    attr->type = itab[inum].type;
    attr->size = itab[inum].size;
    attr->nlink = itab[inum].nlink;
    fs_unlock(flags);
    return 0;
}

int fs_read(uint32_t inum, void* buf, uint32_t off, uint32_t n) {
    uint32_t flags = fs_lock();
    if (inum >= FS_NINODES || itab[inum].type == FS_DIR) {
        fs_unlock(flags);
        return -1;
    }
    struct fs_inode* ip = &itab[inum];
    if (off >= ip->size) {
        fs_unlock(flags);
        return 0;
    }
    if (n > ip->size - off) n = ip->size - off;
    uint8_t* dst = (uint8_t*)buf;
    uint32_t done = 0;
    while (done < n) {
        uint32_t idx = off / FS_BSIZE;
        uint32_t inoff = off % FS_BSIZE;
        uint32_t chunk = FS_BSIZE - inoff;
        if (chunk > n - done) chunk = n - done;
        uint32_t block = inode_block(ip, idx, 0);
        if (!block) {
            fs_unlock(flags);
            return -1;
        }
        k_memcpy(dst, fs_block[block] + inoff, chunk);
        dst += chunk; off += chunk; done += chunk;
    }
    fs_unlock(flags);
    return (int)done;
}

int fs_write_at(uint32_t inum, const void* buf, uint32_t off, uint32_t n) {
    uint32_t flags = fs_lock();
    if (inum >= FS_NINODES || itab[inum].type != FS_FILE) {
        fs_unlock(flags);
        return -1;
    }
    struct fs_inode* ip = &itab[inum];
    if (off > ip->size) {
        fs_unlock(flags);
        return -1;
    }
    const uint8_t* src = (const uint8_t*)buf;
    uint32_t done = 0;
    while (done < n) {
        uint32_t absolute = off + done;
        uint32_t idx = absolute / FS_BSIZE;
        if (idx >= FS_MAXFILE_BLOCKS) {
            fs_unlock(flags);
            return -1;
        }
        uint32_t block = inode_block(ip, idx, 1);
        if (!block) {
            fs_unlock(flags);
            return -1;
        }
        uint32_t inoff = absolute % FS_BSIZE;
        uint32_t chunk = FS_BSIZE - inoff;
        if (chunk > n - done) chunk = n - done;
        k_memcpy(fs_block[block] + inoff, src, chunk);
        fs_mark_dirty(block);
        src += chunk;
        done += chunk;
        if (absolute + chunk > ip->size) {
            ip->size = absolute + chunk;
            fs_mark_dirty(1);
        }
    }
    fs_mark_dirty(1);
    fs_sync();
    fs_unlock(flags);
    return (int)done;
}

int fs_write(uint32_t inum, const void* buf, uint32_t n, int append) {
    uint32_t flags = fs_lock();
    if (inum >= FS_NINODES || itab[inum].type != FS_FILE) {
        fs_unlock(flags);
        return -1;
    }
    struct fs_inode* ip = &itab[inum];
    if (!append) inode_truncate(inum);
    int result = fs_write_at(inum, buf, append ? ip->size : 0, n);
    fs_unlock(flags);
    return result;
}

int fs_truncate(uint32_t inum) {
    uint32_t flags = fs_lock();
    if (inum >= FS_NINODES || itab[inum].type != FS_FILE) {
        fs_unlock(flags);
        return -1;
    }
    inode_truncate(inum);
    fs_mark_dirty(1);
    fs_sync();
    fs_unlock(flags);
    return 0;
}

int fs_inode_type(uint32_t inum) {
    uint32_t flags = fs_lock();
    int result = inum >= FS_NINODES ? -1 : (int)itab[inum].type;
    fs_unlock(flags);
    return result;
}

int fs_inode_size(uint32_t inum) {
    uint32_t flags = fs_lock();
    int result = inum >= FS_NINODES || itab[inum].type != FS_FILE
                 ? -1 : (int)itab[inum].size;
    fs_unlock(flags);
    return result;
}

int fs_readdir(uint32_t inum, uint32_t idx,
               char* name, uint32_t* entry_inum, uint32_t* type, uint32_t* size) {
    uint32_t flags = fs_lock();
    if (inum >= FS_NINODES) {
        fs_unlock(flags);
        return -1;
    }
    struct fs_inode* dp = &itab[inum];
    if (dp->type != FS_DIR) {
        fs_unlock(flags);
        return -1;
    }
    uint32_t nent = dp->size / sizeof(struct fs_dirent);
    if (idx >= nent) {
        fs_unlock(flags);
        return 0;                                // 迭代结束
    }
    // ★原始槽读取: 删除留下的空槽 (inum=0) 原样返回, 由调用方跳过 —
    // 内部跳过会导致调用方 idx+1 后把同一个条目再读一遍 (实测 ls 重复打印)。
    uint32_t block = inode_block(dp, idx / 128, 0);
    if (!block) {
        fs_unlock(flags);
        return -1;
    }
    struct fs_dirent* de = (struct fs_dirent*)fs_block[block] + (idx % 128);
    k_strcpy(name, de->name);
    *entry_inum = de->inum;
    *type = de->inum ? itab[de->inum].type : 0;
    *size = de->inum ? itab[de->inum].size : 0;
    fs_unlock(flags);
    return 1;
}

int fs_chdir(const char* path) {
    uint32_t flags = fs_lock();
    int inum = fs_lookup(path);
    if (inum < 0 || itab[inum].type != FS_DIR) {
        fs_unlock(flags);
        return -1;
    }
    task_t* t = task_current();
    if (!t) {
        fs_unlock(flags);
        return -1;
    }
    t->cwd_inum = (uint32_t)inum;
    fs_unlock(flags);
    return 0;
}

// 从 cwd 一路 ".." 走到根, 收集每段名字, 再反转拼出绝对路径
void fs_pwd(char* out) {
    uint32_t flags = fs_lock();
    char segs[32][FS_MAXNAME + 1];
    uint32_t cur = current_cwd();
    int depth = 0;
    while (cur != sb->root_inum && depth < 32) {
        int parent = dir_lookup(cur, "..");
        if (parent < 0) break;
        // 在 parent 的目录项里找指向 cur 的名字
        uint32_t nent = itab[parent].size / sizeof(struct fs_dirent);
        for (uint32_t i = 0; i < nent; i++) {
            uint32_t block = inode_block(&itab[parent], i / 128, 0);
            if (!block) break;
            struct fs_dirent* de = (struct fs_dirent*)fs_block[block]
                                 + (i % 128);
            if (de->inum == cur && k_strcmp(de->name, ".") != 0
                && k_strcmp(de->name, "..") != 0) {
                k_strcpy(segs[depth], de->name);
                depth++;
                break;
            }
        }
        cur = (uint32_t)parent;
    }
    out[0] = '/';
    int pos = 1;
    for (int i = depth - 1; i >= 0; i--) {
        uint32_t len = k_strlen(segs[i]);
        if (pos + len + 1 >= FS_PATH_MAX) break;   // 防溢出 (深度超限截断)
        for (uint32_t j = 0; j < len; j++) out[pos++] = segs[i][j];
        if (i > 0) out[pos++] = '/';
    }
    out[pos] = '\0';
    fs_unlock(flags);
}

void fs_stat(uint32_t* free_blocks, uint32_t* free_inodes,
             uint32_t* total_blocks, uint32_t* total_inodes) {
    uint32_t flags = fs_lock();
    *free_blocks = sb->free_blocks;
    *free_inodes = sb->free_inodes;
    *total_blocks = sb->nblocks;
    *total_inodes = sb->ninodes;
    fs_unlock(flags);
}

// ---------- 挂载检查 ----------
static uint32_t bitmap_free_count(const uint8_t* bmap, uint32_t n) {
    uint32_t free_count = 0;
    for (uint32_t i = 0; i < n; i++)
        free_count += bit_get(bmap, i) == 0;
    return free_count;
}

static int dirent_name_valid(const struct fs_dirent* de) {
    for (uint32_t i = 0; i <= FS_MAXNAME; i++)
        if (de->name[i] == '\0') return i != 0;
    return 0;
}

// Structural check for the on-disk format. Counter drift is repaired;
// duplicate, dangling, reserved or out-of-range block references are fatal.
static int fs_validate(void) {
    if (sb->magic != FS_MAGIC || sb->nblocks != FS_NBLOCKS ||
        sb->ninodes != FS_NINODES || sb->root_inum != 1)
        return 0;
    if (!bit_get(sb->block_bmap, 0) || !bit_get(sb->block_bmap, 1) ||
        !bit_get(sb->inode_bmap, 0))
        return 0;
    if (itab[sb->root_inum].type != FS_DIR)
        return 0;

    uint8_t referenced[FS_NBLOCKS];
    k_memset(referenced, 0, sizeof(referenced));
    referenced[0] = referenced[1] = 1;

    for (uint32_t i = 1; i < FS_NINODES; i++) {
        struct fs_inode* ip = &itab[i];
        if (!bit_get(sb->inode_bmap, i)) {
            if (ip->type != 0) return 0;
            continue;
        }
        if (ip->type != FS_FILE && ip->type != FS_DIR) return 0;
        if (!ip->nlink) return 0;
        if (ip->size > FS_MAXFILE_BLOCKS * FS_BSIZE) return 0;
        if (ip->type == FS_DIR && (ip->size % sizeof(struct fs_dirent)))
            return 0;

        uint32_t used_blocks = (ip->size + FS_BSIZE - 1) / FS_BSIZE;
        for (uint32_t b = 0; b < FS_NDIRECT; b++) {
            uint32_t block = ip->direct[b];
            if (b >= used_blocks) {
                if (block) return 0;
                continue;
            }
            if (block < 2 || block >= FS_NBLOCKS) return 0;
            if (!bit_get(sb->block_bmap, block) || referenced[block])
                return 0;
            referenced[block] = 1;
        }

        if (used_blocks <= FS_NDIRECT) {
            if (ip->indirect) return 0;
        } else {
            uint32_t table = ip->indirect;
            if (table < 2 || table >= FS_NBLOCKS ||
                !bit_get(sb->block_bmap, table) || referenced[table])
                return 0;
            referenced[table] = 1;
            uint32_t* pointers = (uint32_t*)fs_block[table];
            uint32_t indirect_used = used_blocks - FS_NDIRECT;
            for (uint32_t p = 0; p < FS_PTRS_PER_BLOCK; p++) {
                uint32_t block = pointers[p];
                if (p >= indirect_used) {
                    if (block) return 0;
                    continue;
                }
                if (block < 2 || block >= FS_NBLOCKS ||
                    !bit_get(sb->block_bmap, block) || referenced[block])
                    return 0;
                referenced[block] = 1;
            }
        }

        if (ip->type == FS_DIR) {
            uint32_t nent = ip->size / sizeof(struct fs_dirent);
            int has_dot = 0;
            int has_dotdot = 0;
            for (uint32_t e = 0; e < nent; e++) {
                uint32_t block = inode_block(ip, e / 128, 0);
                if (!block) return 0;
                struct fs_dirent* de = (struct fs_dirent*)fs_block[block]
                                     + (e % 128);
                if (!de->inum) continue;
                if (de->inum >= FS_NINODES ||
                    !bit_get(sb->inode_bmap, de->inum) ||
                    !dirent_name_valid(de))
                    return 0;
                if (k_strcmp(de->name, ".") == 0) has_dot = 1;
                if (k_strcmp(de->name, "..") == 0) has_dotdot = 1;
            }
            if (!has_dot || !has_dotdot) return 0;
        }
    }

    for (uint32_t b = 2; b < FS_NBLOCKS; b++) {
        if (bit_get(sb->block_bmap, b) != referenced[b]) return 0;
    }

    uint32_t free_blocks = bitmap_free_count(sb->block_bmap, FS_NBLOCKS);
    uint32_t free_inodes = bitmap_free_count(sb->inode_bmap, FS_NINODES);
    if (free_blocks != sb->free_blocks || free_inodes != sb->free_inodes) {
        serial_puts("[FS] repaired allocation counters\n\r");
        sb->free_blocks = free_blocks;
        sb->free_inodes = free_inodes;
        fs_mark_dirty(0);
    }
    return 1;
}

static void fs_set_cwds(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_t* t = task_at(i);
        if (t) t->cwd_inum = sb->root_inum;
    }
}

static int fs_read_disk(void) {
    for (uint32_t b = 0; b < FS_NBLOCKS; b++) {
        uint32_t base = FS_DISK_BASE_LBA + b * FS_BLOCK_SECTORS;
        for (uint32_t s = 0; s < FS_BLOCK_SECTORS; s++) {
            if (ata_read_sector(base + s,
                                fs_block[b] + s * ATA_SECTOR_SIZE) < 0)
                return -1;
        }
    }
    return 0;
}

static void fs_format(int on_disk) {
    for (uint32_t i = 0; i < FS_NBLOCKS; i++)
        k_memset(fs_block[i], 0, FS_BSIZE);

    sb = (struct fs_super*)fs_block[0];
    itab = (struct fs_inode*)fs_block[1];
    sb->magic = FS_MAGIC;
    sb->nblocks = FS_NBLOCKS;
    sb->ninodes = FS_NINODES;
    sb->root_inum = 1;                 // inode 0 保留, 根 = 1
    sb->free_blocks = FS_NBLOCKS;
    sb->free_inodes = FS_NINODES;
    k_memset(sb->inode_bmap, 0, sizeof(sb->inode_bmap));
    k_memset(sb->block_bmap, 0, sizeof(sb->block_bmap));
    fs_mark_dirty(0);
    fs_mark_dirty(1);

    // 块 0/1 是超级块与 inode 表；块 2 是根目录数据。
    bit_set(sb->block_bmap, 0);
    bit_set(sb->block_bmap, 1);
    sb->free_blocks -= 2;
    int rootdir = balloc();

    // inode 0 永久保留，目录项用 inum=0 表示空槽。
    bit_set(sb->inode_bmap, 0);
    sb->free_inodes--;

    int root = ialloc(FS_DIR);
    itab[root].direct[0] = (uint32_t)rootdir;
    fs_mark_dirty(1);
    dir_add((uint32_t)root, ".", (uint32_t)root);
    dir_add((uint32_t)root, "..", (uint32_t)root);
    fs_set_cwds();

    serial_puts(on_disk ? "[FS] formatted disk: " : "[FS] formatted: ");
    serial_put_hex(FS_NBLOCKS);
    serial_puts(" blocks x "); serial_put_hex(FS_BSIZE);
    serial_puts(" bytes, "); serial_put_hex(FS_NINODES);
    serial_puts(" inodes\n\r");

    if (on_disk) fs_sync();
}

// ---------- 初始化 + 自测 ----------
static void fs_selftest(void) {
    // 创建 → 写 → 读回比对 → 删除, 全过程验证
    static const char* msg = "NewOS filesystem selftest 0123456789";
    char buf[64];
    int ok = 1;

    int inum = fs_create("/__fstest.txt", FS_FILE);
    if (inum < 0) ok = 0;
    if (ok && fs_write((uint32_t)inum, msg, k_strlen(msg), 0) < 0) ok = 0;
    k_memset(buf, 0, sizeof(buf));
    if (ok && fs_read((uint32_t)inum, buf, 0, k_strlen(msg)) != (int)k_strlen(msg)) ok = 0;
    if (ok && k_memcmp(buf, msg, k_strlen(msg)) != 0) ok = 0;
    // 追加模式
    if (ok && fs_write((uint32_t)inum, "!", 1, 1) != 1) ok = 0;
    if (ok && fs_read((uint32_t)inum, buf, k_strlen(msg), 1) != 1) ok = 0;
    if (ok && buf[0] != '!') ok = 0;
    // 截断重写
    if (ok && fs_write((uint32_t)inum, "x", 1, 0) != 1) ok = 0;
    if (ok && itab[inum].size != 1) ok = 0;
    if (ok && fs_remove("/__fstest.txt") < 0) ok = 0;
    if (ok && itab[inum].type != 0) ok = 0;          // inode 已释放
    if (ok && fs_lookup("/__fstest.txt") >= 0) ok = 0;

    serial_puts("[FS] selftest ");
    serial_puts(ok ? "PASS\n\r" : "FAIL\n\r");
}

void fs_init(void) {
    // 1. Allocate a fixed 256-page block cache.
    for (int i = 0; i < FS_NBLOCKS; i++) {
        fs_block[i] = (uint8_t*)(uint32_t)alloc_page();
        if (fs_block[i] == 0) {
            serial_puts("[FS] FATAL: out of memory\n\r");
            return;
        }
        k_memset(fs_block[i], 0, FS_BSIZE);
    }
    sb = (struct fs_super*)fs_block[0];
    itab = (struct fs_inode*)fs_block[1];
    k_memset(fs_dirty, 0, sizeof(fs_dirty));
    fs_disk_mounted = ata_init() == 0;

    int disk_readable = fs_disk_mounted && fs_read_disk() == 0;
    if (disk_readable && sb->magic == FS_MAGIC && fs_validate()) {
        fs_set_cwds();
        serial_puts("[FS] mounted disk at LBA ");
        serial_put_hex(FS_DISK_BASE_LBA);
        serial_puts(", "); serial_put_hex(sb->nblocks);
        serial_puts(" blocks, "); serial_put_hex(sb->free_blocks);
        serial_puts(" free\n\r");
        fs_sync();
    } else {
        if (fs_disk_mounted && disk_readable && sb->magic == FS_MAGIC) {
            // Never overwrite a damaged filesystem automatically. Mount a
            // volatile empty FS so diagnostics remain available.
            serial_puts("[FS] FATAL: damaged disk filesystem, mount volatile\n\r");
            fs_disk_mounted = 0;
        } else if (fs_disk_mounted && disk_readable) {
            serial_puts("[FS] no filesystem on disk, formatting\n\r");
        }
        fs_format(fs_disk_mounted);
    }

    fs_selftest();
}
