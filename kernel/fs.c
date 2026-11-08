// ============================================================
//  持久化磁盘文件系统实现 (见 fs.h 的布局说明)
//
//  块与页的映射: fs_block[i] 存第 i 个逻辑块对应的物理地址
//  (alloc_page 分配, 不要求物理连续)。A 盘上的逻辑块从 LBA 2048
//  开始线性映射。共享缓存由可睡眠 mutex 保护；dirty 块按阈值
//  延迟回写，元数据先写 redo 日志再改 home blocks。
// ============================================================

#include "fs.h"
#include "ata.h"
#include "string.h"
#include "memory.h"
#include "serial.h"
#include "task.h"
#include "timer.h"
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
#define FS_JOURNAL_LBA 64u               // 前 1MB 内的 redo 日志头
#define FS_JOURNAL_DATA_LBA 80u          // 日志数据块区
#define FS_JOURNAL_BLOCKS 120u           // 单事务最多 120 个元数据块
#define FS_DIRTY_LIMIT 24u               // 脏块达到此值就回写
#define FS_FLUSH_TICKS 18u               // 最迟约 1 秒回写一次
#define FS_JOURNAL_MAGIC 0x4C4E524Au    // "JRNL" (小端)

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
static uint8_t fs_meta_dirty[FS_NBLOCKS / 8];
static uint32_t fs_dirty_count;
static uint32_t fs_last_flush_tick;
static int fs_disk_mounted;
static volatile int fs_mutex_held;
static int fs_mutex_owner_pid;
static uint32_t fs_journal_seq;

static int fs_block_dirty(uint32_t block);
static uint32_t inode_block(struct fs_inode* ip, uint32_t idx, int create);
static int ialloc(uint32_t type);
static int dir_lookup(uint32_t dinum, const char* name);
static int dir_add(uint32_t dinum, const char* name, uint32_t inum);

static void fs_mark_dirty(uint32_t block) {
    if (block < FS_NBLOCKS && !fs_block_dirty(block)) {
        fs_dirty[block / 8] |= (uint8_t)(1u << (block % 8));
        fs_dirty_count++;
    }
}

static void fs_mark_meta_dirty(uint32_t block) {
    if (block >= FS_NBLOCKS) return;
    fs_mark_dirty(block);
    fs_meta_dirty[block / 8] |= (uint8_t)(1u << (block % 8));
}

static int fs_block_dirty(uint32_t block) {
    return (fs_dirty[block / 8] >> (block % 8)) & 1;
}

static int fs_block_meta_dirty(uint32_t block) {
    return (fs_meta_dirty[block / 8] >> (block % 8)) & 1;
}

static void fs_mark_clean(uint32_t block) {
    if (!fs_block_dirty(block)) return;
    fs_dirty[block / 8] &= (uint8_t)~(1u << (block % 8));
    if (fs_dirty_count) fs_dirty_count--;
}

static void fs_mark_meta_clean(uint32_t block) {
    fs_meta_dirty[block / 8] &= (uint8_t)~(1u << (block % 8));
}

// Single-CPU sleeping mutex. Only the lock word is protected by cli;
// the filesystem operation itself remains preemptible. A second task
// trying to enter sleeps on TASK_WAIT_FS instead of spinning or blocking
// the timer/keyboard IRQs for the whole ATA transaction.
static uint32_t fs_lock(void) {
    task_t* t = task_current();
    int pid = t ? (int)t->pid : 0;
    for (;;) {
        __asm__ volatile("cli" ::: "memory");
        if (!fs_mutex_held) {
            fs_mutex_held = 1;
            fs_mutex_owner_pid = pid;
            __asm__ volatile("sti" ::: "memory");
            return 0;
        }
        task_sleep_switch_locked(TASK_WAIT_FS, 0);
        // Woken while still cli; re-check the lock and release only on entry.
    }
}

static void fs_unlock(uint32_t flags) {
    (void)flags;
    __asm__ volatile("cli" ::: "memory");
    fs_mutex_held = 0;
    fs_mutex_owner_pid = 0;
    task_wake_fs();
    __asm__ volatile("sti" ::: "memory");
}

struct fs_journal_header {
    uint32_t magic;
    uint32_t sequence;
    uint32_t count;
    uint32_t checksum;
    uint32_t blocks[FS_JOURNAL_BLOCKS];
};

typedef char fs_journal_header_fits_sector[
    sizeof(struct fs_journal_header) <= ATA_SECTOR_SIZE ? 1 : -1];

static uint32_t fs_journal_checksum(const struct fs_journal_header* h) {
    uint32_t sum = 2166136261u;
    sum = (sum ^ h->sequence) * 16777619u;
    sum = (sum ^ h->count) * 16777619u;
    for (uint32_t i = 0; i < h->count; i++)
        sum = (sum ^ h->blocks[i]) * 16777619u;
    return sum;
}

static int fs_write_home_block_locked(uint32_t block) {
    uint32_t base = FS_DISK_BASE_LBA + block * FS_BLOCK_SECTORS;
    for (uint32_t s = 0; s < FS_BLOCK_SECTORS; s++) {
        if (ata_write_sector(base + s,
                             fs_block[block] + s * ATA_SECTOR_SIZE) < 0)
            return -1;
    }
    return 0;
}

static int fs_journal_clear_locked(void) {
    uint8_t zero[ATA_SECTOR_SIZE];
    k_memset(zero, 0, sizeof(zero));
    if (ata_write_sector(FS_JOURNAL_LBA, zero) < 0) return -1;
    return ata_flush();
}

// Stage a committed redo transaction. At return the payload and header are
// durable, while home metadata is deliberately still untouched.
static int fs_journal_stage_metadata_locked(uint32_t blocks[], uint32_t* out_count) {
    uint32_t count = 0;
    for (uint32_t b = 0; b < FS_NBLOCKS; b++) {
        if (!fs_block_meta_dirty(b)) continue;
        if (count >= FS_JOURNAL_BLOCKS) {
            serial_puts("[FS] ERROR: metadata transaction exceeds journal\n\r");
            return -1;
        }
        blocks[count++] = b;
    }
    *out_count = count;
    if (!count) return 0;

    if (fs_journal_clear_locked() < 0) return -1;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t payload = FS_JOURNAL_DATA_LBA + i * FS_BLOCK_SECTORS;
        for (uint32_t s = 0; s < FS_BLOCK_SECTORS; s++) {
            if (ata_write_sector(payload + s,
                                 fs_block[blocks[i]] + s * ATA_SECTOR_SIZE) < 0)
                return -1;
        }
    }
    if (ata_flush() < 0) return -1;

    uint8_t sector[ATA_SECTOR_SIZE];
    k_memset(sector, 0, sizeof(sector));
    struct fs_journal_header* h = (struct fs_journal_header*)sector;
    h->magic = FS_JOURNAL_MAGIC;
    h->sequence = ++fs_journal_seq;
    h->count = count;
    for (uint32_t i = 0; i < count; i++) h->blocks[i] = blocks[i];
    h->checksum = fs_journal_checksum(h);
    if (ata_write_sector(FS_JOURNAL_LBA, sector) < 0 || ata_flush() < 0)
        return -1;
    return 0;
}

static int fs_journal_finish_metadata_locked(const uint32_t blocks[],
                                              uint32_t count) {
    if (!count) return 0;
    for (uint32_t i = 0; i < count; i++) {
        if (fs_write_home_block_locked(blocks[i]) < 0) return -1;
    }
    if (ata_flush() < 0) return -1;
    for (uint32_t i = 0; i < count; i++) {
        fs_mark_clean(blocks[i]);
        fs_mark_meta_clean(blocks[i]);
    }
    if (fs_journal_clear_locked() < 0) return -1;
    serial_puts("[FS] journal commit seq=");
    serial_put_hex(fs_journal_seq);
    serial_puts(" blocks=");
    serial_put_hex(count);
    serial_puts("\n\r");
    return 0;
}

// Redo journal: payload and committed header are stable before any home
// metadata block is touched. A committed transaction is replayed
// idempotently after a crash; an uncommitted one is discarded.
static int fs_journal_metadata_locked(void) {
    uint32_t blocks[FS_JOURNAL_BLOCKS];
    uint32_t count = 0;
    if (fs_journal_stage_metadata_locked(blocks, &count) < 0) return -1;
    return fs_journal_finish_metadata_locked(blocks, count);
}

static int fs_write_dirty_data_locked(void) {
    int wrote = 0;
    for (uint32_t b = 0; b < FS_NBLOCKS; b++) {
        if (!fs_block_dirty(b) || fs_block_meta_dirty(b)) continue;
        if (fs_write_home_block_locked(b) < 0) return -1;
        wrote = 1;
    }
    if (wrote && ata_flush() < 0) return -1;
    for (uint32_t b = 0; b < FS_NBLOCKS; b++)
        if (fs_block_dirty(b) && !fs_block_meta_dirty(b)) fs_mark_clean(b);
    return 0;
}

static int fs_sync_locked(void) {
    if (!fs_disk_mounted) {
        k_memset(fs_dirty, 0, sizeof(fs_dirty));
        k_memset(fs_meta_dirty, 0, sizeof(fs_meta_dirty));
        fs_dirty_count = 0;
        return 0;
    }

    // File data is persisted before metadata that makes it reachable.
    // This way a crash can lose recent payload, but not expose a valid
    // inode/directory graph pointing at never-written data blocks.
    if (fs_write_dirty_data_locked() < 0) return -1;

    if (fs_journal_metadata_locked() < 0) return -1;
    fs_last_flush_tick = g_ticks;
    return 0;
}

static int fs_flush_policy_locked(int force) {
    if (!fs_dirty_count) return 0;
    if (!force && fs_dirty_count < FS_DIRTY_LIMIT &&
        (uint32_t)(g_ticks - fs_last_flush_tick) < FS_FLUSH_TICKS)
        return 0;
    return fs_sync_locked();
}

static int fs_sync(void) {
    uint32_t flags = fs_lock();
    int result = fs_sync_locked();
    fs_unlock(flags);
    return result;
}

int fs_flush(void) {
    return fs_sync();
}

void fs_cache_stats(uint32_t* dirty_blocks, uint32_t* dirty_metadata) {
    uint32_t flags = fs_lock();
    uint32_t meta = 0;
    for (uint32_t b = 0; b < FS_NBLOCKS; b++)
        if (fs_block_meta_dirty(b)) meta++;
    if (dirty_blocks) *dirty_blocks = fs_dirty_count;
    if (dirty_metadata) *dirty_metadata = meta;
    fs_unlock(flags);
}

void fs_periodic_flush(void) {
    __asm__ volatile("cli" ::: "memory");
    if (fs_mutex_held) {
        __asm__ volatile("sti" ::: "memory");
        return;
    }
    fs_mutex_held = 1;
    fs_mutex_owner_pid = 0;
    __asm__ volatile("sti" ::: "memory");
    if (fs_dirty_count &&
        (uint32_t)(g_ticks - fs_last_flush_tick) >= FS_FLUSH_TICKS) {
        fs_sync_locked();
    }
    fs_unlock(0);
}

// QEMU-only crash injection: commit a metadata transaction to the journal,
// then stop before touching its home blocks. The next boot must replay it.
void fs_crashpoint(void) {
    uint32_t flags = fs_lock();
    const char* name = "__journal-crash.txt";
    if (!fs_disk_mounted || dir_lookup(sb->root_inum, name) >= 0) {
        serial_puts("[FS] crashpoint: unavailable\n\r");
        fs_unlock(flags);
        return;
    }

    int inum = ialloc(FS_FILE);
    if (inum < 0 || dir_add(sb->root_inum, name, (uint32_t)inum) < 0) {
        serial_puts("[FS] crashpoint: create failed\n\r");
        fs_unlock(flags);
        return;
    }
    static const char payload[] = "journal replay restored this file\n";
    struct fs_inode* ip = &itab[inum];
    uint32_t block = inode_block(ip, 0, 1);
    if (!block) {
        serial_puts("[FS] crashpoint: block allocation failed\n\r");
        fs_unlock(flags);
        return;
    }
    k_memcpy(fs_block[block], payload, sizeof(payload) - 1);
    ip->size = sizeof(payload) - 1;
    fs_mark_dirty(block);
    fs_mark_meta_dirty(1);
    if (fs_write_dirty_data_locked() < 0) {
        serial_puts("[FS] crashpoint: data write failed\n\r");
        fs_unlock(flags);
        return;
    }

    uint32_t blocks[FS_JOURNAL_BLOCKS];
    uint32_t count = 0;
    if (fs_journal_stage_metadata_locked(blocks, &count) < 0) {
        serial_puts("[FS] crashpoint: journal stage failed\n\r");
        fs_unlock(flags);
        return;
    }
    serial_puts("[FS] crashpoint armed seq=");
    serial_put_hex(fs_journal_seq);
    serial_puts(" meta=");
    serial_put_hex(count);
    serial_puts("\n\r");
    __asm__ volatile("cli");
    while (1) __asm__ volatile("hlt");
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
    fs_mark_meta_dirty(0);
    fs_mark_dirty((uint32_t)b);
    return b;
}

static void bfree(uint32_t b) {
    bit_clear(sb->block_bmap, b);
    sb->free_blocks++;
    fs_mark_meta_dirty(0);
}

// Resolve logical data block idx. With create=0 this is a read-only lookup;
// with create=1 missing indirect/data blocks are allocated and zeroed.
static uint32_t inode_block(struct fs_inode* ip, uint32_t idx, int create) {
    if (idx < FS_NDIRECT) {
        if (!ip->direct[idx] && create) {
            int b = balloc();
            if (b < 0) return 0;
            ip->direct[idx] = (uint32_t)b;
            fs_mark_meta_dirty(1);
        }
        return ip->direct[idx];
    }
    if (idx >= FS_MAXFILE_BLOCKS) return 0;

    if (!ip->indirect) {
        if (!create) return 0;
        int table = balloc();
        if (table < 0) return 0;
        ip->indirect = (uint32_t)table;
        fs_mark_meta_dirty(1);
    }

    uint32_t slot = idx - FS_NDIRECT;
    uint32_t* pointers = (uint32_t*)fs_block[ip->indirect];
    if (!pointers[slot] && create) {
        int b = balloc();
        if (b < 0) return 0;
        pointers[slot] = (uint32_t)b;
        fs_mark_meta_dirty(ip->indirect);
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
    fs_mark_meta_dirty(0);
    fs_mark_meta_dirty(1);
    return i;
}

static void ifree(uint32_t i) {
    bit_clear(sb->inode_bmap, i);
    itab[i].type = 0;
    sb->free_inodes++;
    fs_mark_meta_dirty(0);
    fs_mark_meta_dirty(1);
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
    fs_mark_meta_dirty(1);
}

static void inode_drop_block(struct fs_inode* ip, uint32_t idx) {
    uint32_t block = 0;
    if (idx < FS_NDIRECT) {
        block = ip->direct[idx];
        ip->direct[idx] = 0;
    } else if (ip->indirect) {
        uint32_t slot = idx - FS_NDIRECT;
        uint32_t* pointers = (uint32_t*)fs_block[ip->indirect];
        block = pointers[slot];
        pointers[slot] = 0;
        fs_mark_meta_dirty(ip->indirect);
    }
    if (block) bfree(block);
}

// ftruncate: shrink frees tail blocks; grow allocates zeroed blocks and
// explicitly zeroes the extension gap inside the previous last block.
static int inode_truncate_size(uint32_t inum, uint32_t size) {
    struct fs_inode* ip = &itab[inum];
    uint32_t max_size = FS_MAXFILE_BLOCKS * FS_BSIZE;
    if (ip->type != FS_FILE || size > max_size) return -1;
    uint32_t old = ip->size;
    uint32_t old_blocks = (old + FS_BSIZE - 1) / FS_BSIZE;
    uint32_t new_blocks = (size + FS_BSIZE - 1) / FS_BSIZE;

    if (size < old) {
        if ((size % FS_BSIZE) && new_blocks) {
            uint32_t block = inode_block(ip, new_blocks - 1, 0);
            if (block) {
                uint32_t clear_from = size % FS_BSIZE;
                uint32_t clear_to = old < new_blocks * FS_BSIZE
                    ? old % FS_BSIZE : FS_BSIZE;
                if (clear_to == 0) clear_to = FS_BSIZE;
                k_memset(fs_block[block] + clear_from, 0,
                         clear_to - clear_from);
                fs_mark_dirty(block);
            }
        }
        for (uint32_t idx = new_blocks; idx < old_blocks; idx++)
            inode_drop_block(ip, idx);
        if (new_blocks <= FS_NDIRECT && ip->indirect) {
            uint32_t table = ip->indirect;
            uint32_t* pointers = (uint32_t*)fs_block[table];
            for (uint32_t i = 0; i < FS_PTRS_PER_BLOCK; i++) {
                if (pointers[i]) bfree(pointers[i]);
                pointers[i] = 0;
            }
            bfree(table);
            ip->indirect = 0;
        }
    } else if (size > old) {
        if (old % FS_BSIZE) {
            uint32_t block = inode_block(ip, old / FS_BSIZE, 0);
            if (!block) return -1;
            uint32_t from = old % FS_BSIZE;
            uint32_t until = size < (old / FS_BSIZE + 1) * FS_BSIZE
                ? size % FS_BSIZE : FS_BSIZE;
            k_memset(fs_block[block] + from, 0, until - from);
            fs_mark_dirty(block);
        }
        for (uint32_t idx = old_blocks; idx < new_blocks; idx++) {
            if (!inode_block(ip, idx, 1)) return -1;
        }
    }

    ip->size = size;
    fs_mark_meta_dirty(1);
    return 0;
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
    fs_mark_meta_dirty(block);
    fs_mark_meta_dirty(1);
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
            fs_mark_meta_dirty(block);
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
            fs_mark_meta_dirty(block);
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
static int fs_lookup_locked(const char* path) {
    char last[FS_MAXNAME + 1];
    int parent = namex(path, last);
    int result = -1;
    if (parent >= 0) {
        result = k_strcmp(last, ".") == 0 ? parent
                 : dir_lookup((uint32_t)parent, last);
    }
    return result;
}

int fs_lookup(const char* path) {
    uint32_t flags = fs_lock();
    int result = fs_lookup_locked(path);
    fs_unlock(flags);
    return result;
}

int fs_size(const char* path) {
    uint32_t flags = fs_lock();
    int inum = fs_lookup_locked(path);
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
    fs_mark_meta_dirty(1);
    fs_flush_policy_locked(1);
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
    fs_mark_meta_dirty(1);
    fs_flush_policy_locked(1);
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
    if (inum < 0 || itab[newparent].type != FS_DIR) {
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

    int target = dir_lookup((uint32_t)newparent, newname);
    if (target == inum) {
        fs_unlock(flags);
        return 0;
    }
    if (target >= 0) {
        struct fs_inode* tip = &itab[target];
        if (tip->type != ip->type) {
            fs_unlock(flags);
            return -1;
        }
        if (tip->type == FS_DIR &&
            (target == (int)sb->root_inum ||
             is_task_cwd_ancestor((uint32_t)target) ||
             !dir_is_empty((uint32_t)target))) {
            fs_unlock(flags);
            return -1;
        }
    }

    if (oldparent == newparent) {
        if (target >= 0 &&
            dir_remove((uint32_t)newparent, newname) < 0) {
            fs_unlock(flags);
            return -1;
        }
        if (dir_replace((uint32_t)oldparent, oldname, newname,
                        (uint32_t)inum) < 0) {
            fs_unlock(flags);
            return -1;
        }
    } else {
        int placed = target >= 0
            ? dir_replace((uint32_t)newparent, newname, 0, (uint32_t)inum)
            : dir_add((uint32_t)newparent, newname, (uint32_t)inum);
        if (placed < 0) {
            fs_unlock(flags);
            return -1;
        }
        if (dir_remove((uint32_t)oldparent, oldname) < 0) {
            if (target < 0) dir_remove((uint32_t)newparent, newname);
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

    if (target >= 0) {
        struct fs_inode* tip = &itab[target];
        if (tip->type == FS_DIR) {
            itab[newparent].nlink--;
        }
        tip->nlink--;
        if (!tip->nlink) {
            inode_truncate((uint32_t)target);
            ifree((uint32_t)target);
        }
    }

    fs_mark_meta_dirty(1);
    fs_flush_policy_locked(1);
    fs_unlock(flags);
    return 0;
}

int fs_link(const char* oldpath, const char* newpath) {
    uint32_t flags = fs_lock();
    int inum = fs_lookup_locked(oldpath);
    if (inum < 0 || itab[inum].type != FS_FILE) {
        fs_unlock(flags);
        return -1;
    }

    char last[FS_MAXNAME + 1];
    int parent = namex(newpath, last);
    if (parent < 0 || itab[parent].type != FS_DIR ||
        k_strcmp(last, ".") == 0 || k_strcmp(last, "..") == 0 ||
        dir_lookup((uint32_t)parent, last) >= 0 ||
        dir_add((uint32_t)parent, last, (uint32_t)inum) < 0) {
        fs_unlock(flags);
        return -1;
    }
    itab[inum].nlink++;
    fs_mark_meta_dirty(1);
    fs_flush_policy_locked(1);
    fs_unlock(flags);
    return 0;
}

int fs_getattr(const char* path, fs_attr_t* attr) {
    if (!attr) return -1;
    uint32_t flags = fs_lock();
    int inum = fs_lookup_locked(path);
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

static int fs_write_at_locked(uint32_t inum, const void* buf,
                              uint32_t off, uint32_t n) {
    if (inum >= FS_NINODES || itab[inum].type != FS_FILE) {
        return -1;
    }
    struct fs_inode* ip = &itab[inum];
    if (off > ip->size) {
        return -1;
    }
    const uint8_t* src = (const uint8_t*)buf;
    uint32_t done = 0;
    while (done < n) {
        uint32_t absolute = off + done;
        uint32_t idx = absolute / FS_BSIZE;
        if (idx >= FS_MAXFILE_BLOCKS) {
            return -1;
        }
        uint32_t block = inode_block(ip, idx, 1);
        if (!block) {
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
            fs_mark_meta_dirty(1);
        }
    }
    fs_mark_meta_dirty(1);
    return (int)done;
}

int fs_write_at(uint32_t inum, const void* buf, uint32_t off, uint32_t n) {
    uint32_t flags = fs_lock();
    int result = fs_write_at_locked(inum, buf, off, n);
    fs_flush_policy_locked(0);
    fs_unlock(flags);
    return result;
}

int fs_write(uint32_t inum, const void* buf, uint32_t n, int append) {
    uint32_t flags = fs_lock();
    if (inum >= FS_NINODES || itab[inum].type != FS_FILE) {
        fs_unlock(flags);
        return -1;
    }
    struct fs_inode* ip = &itab[inum];
    if (!append) inode_truncate(inum);
    int result = fs_write_at_locked(inum, buf, append ? ip->size : 0, n);
    fs_flush_policy_locked(1);
    fs_unlock(flags);
    return result;
}

int fs_truncate_size(uint32_t inum, uint32_t size) {
    uint32_t flags = fs_lock();
    if (inum >= FS_NINODES) {
        fs_unlock(flags);
        return -1;
    }
    int result = inode_truncate_size(inum, size);
    fs_flush_policy_locked(0);
    fs_unlock(flags);
    return result;
}

int fs_truncate(uint32_t inum) {
    return fs_truncate_size(inum, 0);
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
    int inum = fs_lookup_locked(path);
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
        fs_mark_meta_dirty(0);
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

static int fs_journal_replay(void) {
    uint8_t sector[ATA_SECTOR_SIZE];
    if (ata_read_sector(FS_JOURNAL_LBA, sector) < 0) return -1;
    struct fs_journal_header* h = (struct fs_journal_header*)sector;
    if (h->magic != FS_JOURNAL_MAGIC) return 0;
    if (h->count == 0 || h->count > FS_JOURNAL_BLOCKS ||
        h->checksum != fs_journal_checksum(h)) {
        serial_puts("[FS] invalid journal header ignored\n\r");
        return 0;
    }

    serial_puts("[FS] replaying committed journal seq=");
    serial_put_hex(h->sequence);
    serial_puts(" blocks=");
    serial_put_hex(h->count);
    serial_puts("\n\r");

    for (uint32_t i = 0; i < h->count; i++) {
        uint32_t block = h->blocks[i];
        if (block >= FS_NBLOCKS) return -1;
        uint32_t payload = FS_JOURNAL_DATA_LBA + i * FS_BLOCK_SECTORS;
        for (uint32_t s = 0; s < FS_BLOCK_SECTORS; s++) {
            if (ata_read_sector(payload + s,
                                fs_block[block] + s * ATA_SECTOR_SIZE) < 0)
                return -1;
        }
    }
    for (uint32_t i = 0; i < h->count; i++) {
        if (fs_write_home_block_locked(h->blocks[i]) < 0) return -1;
    }
    if (ata_flush() < 0 || fs_journal_clear_locked() < 0) return -1;
    fs_journal_seq = h->sequence;
    serial_puts("[FS] journal replay complete\n\r");
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
    fs_mark_meta_dirty(0);
    fs_mark_meta_dirty(1);

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
    fs_mark_meta_dirty(1);
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
    k_memset(fs_meta_dirty, 0, sizeof(fs_meta_dirty));
    fs_dirty_count = 0;
    fs_mutex_held = 0;
    fs_mutex_owner_pid = 0;
    fs_journal_seq = 0;
    fs_last_flush_tick = g_ticks;
    fs_disk_mounted = ata_init() == 0;

    int disk_readable = fs_disk_mounted && fs_read_disk() == 0;
    if (disk_readable && fs_journal_replay() < 0) {
        serial_puts("[FS] FATAL: journal replay failed\n\r");
        fs_disk_mounted = 0;
        disk_readable = 0;
    }
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
