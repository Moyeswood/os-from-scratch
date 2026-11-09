#include "fd.h"
#include "fs.h"
#include "heap.h"
#include "proc.h"
#include "string.h"
#include "task.h"
#include "user.h"

#define PIPE_CAPACITY 1024

#define FILE_CONSOLE 1
#define FILE_REGULAR 2
#define FILE_PIPE    3

#define PIPE_READ_END  1
#define PIPE_WRITE_END 2

typedef struct {
    uint8_t* data;
    uint32_t head;
    uint32_t tail;
    uint32_t used;
    uint32_t readers;
    uint32_t writers;
} kernel_pipe_t;

struct task_file {
    int refs;
    uint8_t kind;
    uint8_t readable;
    uint8_t writable;
    uint8_t append;
    uint8_t pipe_end;
    uint32_t inum;
    uint32_t off;
    kernel_pipe_t* pipe;
};

static task_file_t console_in = {
    .refs = 1, .kind = FILE_CONSOLE, .readable = 1
};
static task_file_t console_out = {
    .refs = 1, .kind = FILE_CONSOLE, .writable = 1
};

static uint32_t irq_save_disable(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uint32_t flags) {
    if (flags & 0x200) __asm__ volatile("sti" : : : "memory");
}

task_file_t* fd_stdin(void) {
    return &console_in;
}

task_file_t* fd_stdout(void) {
    return &console_out;
}

task_file_t* fd_file_new(uint32_t inum, uint8_t readable,
                         uint8_t writable, uint8_t append) {
    task_file_t* file = (task_file_t*)kmalloc(sizeof(task_file_t));
    if (!file) return 0;
    k_memset(file, 0, sizeof(*file));
    file->refs = 1;
    file->kind = FILE_REGULAR;
    file->inum = inum;
    file->readable = readable;
    file->writable = writable;
    file->append = append;
    return file;
}

static void pipe_destroy(kernel_pipe_t* pipe) {
    if (!pipe) return;
    if (pipe->data) kfree(pipe->data);
    kfree(pipe);
}

int fd_pipe_pair(task_file_t** reader, task_file_t** writer) {
    if (!reader || !writer) return -1;
    *reader = 0;
    *writer = 0;

    kernel_pipe_t* pipe = (kernel_pipe_t*)kmalloc(sizeof(*pipe));
    if (!pipe) return -1;
    k_memset(pipe, 0, sizeof(*pipe));
    pipe->data = (uint8_t*)kmalloc(PIPE_CAPACITY);
    if (!pipe->data) {
        kfree(pipe);
        return -1;
    }
    pipe->readers = 1;
    pipe->writers = 1;

    task_file_t* in = (task_file_t*)kmalloc(sizeof(*in));
    task_file_t* out = (task_file_t*)kmalloc(sizeof(*out));
    if (!in || !out) {
        if (in) kfree(in);
        if (out) kfree(out);
        pipe_destroy(pipe);
        return -1;
    }

    k_memset(in, 0, sizeof(*in));
    in->refs = 1;
    in->kind = FILE_PIPE;
    in->readable = 1;
    in->pipe_end = PIPE_READ_END;
    in->pipe = pipe;

    k_memset(out, 0, sizeof(*out));
    out->refs = 1;
    out->kind = FILE_PIPE;
    out->writable = 1;
    out->pipe_end = PIPE_WRITE_END;
    out->pipe = pipe;

    *reader = in;
    *writer = out;
    return 0;
}

void fd_retain(task_file_t* file) {
    if (file) file->refs++;
}

int fd_is_pipe(task_file_t* file) {
    return file && file->kind == FILE_PIPE;
}

void fd_release(task_file_t* file) {
    if (!file || file == &console_in || file == &console_out) return;
    if (--file->refs > 0) return;

    if (file->kind == FILE_PIPE && file->pipe) {
        kernel_pipe_t* pipe = file->pipe;
        if (file->pipe_end == PIPE_READ_END && pipe->readers) pipe->readers--;
        if (file->pipe_end == PIPE_WRITE_END && pipe->writers) pipe->writers--;
        task_wake_pipe();
        if (!pipe->readers && !pipe->writers) pipe_destroy(pipe);
    }
    kfree(file);
}

int fd_file_read(task_file_t* file, void* buf, uint32_t count) {
    if (!file || file->kind != FILE_REGULAR || !file->readable) return -1;
    int got = fs_read(file->inum, buf, file->off, count);
    if (got > 0) file->off += (uint32_t)got;
    return got;
}

int fd_file_write(task_file_t* file, const void* buf, uint32_t count) {
    if (!file || file->kind != FILE_REGULAR || !file->writable) return -1;
    uint32_t off = file->append ?
        (uint32_t)fs_inode_size((int)file->inum) : file->off;
    int wrote = fs_write_at(file->inum, buf, off, count);
    if (wrote > 0) file->off = off + (uint32_t)wrote;
    return wrote;
}

int fd_file_seek(task_file_t* file, int offset, int whence) {
    if (!file || file->kind != FILE_REGULAR) return -1;
    int base;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (int)file->off;
    else if (whence == SEEK_END) base = fs_inode_size((int)file->inum);
    else return -1;
    int target = base + offset;
    if (target < 0) return -1;
    file->off = (uint32_t)target;
    return target;
}

int fd_pipe_read(task_file_t* file, void* buf, uint32_t count) {
    if (!file || file->kind != FILE_PIPE ||
        file->pipe_end != PIPE_READ_END || !count) return KERR(EINVAL);

    kernel_pipe_t* pipe = file->pipe;
    uint8_t* dst = (uint8_t*)buf;
    uint32_t flags = irq_save_disable();
    for (;;) {
        if (pipe->used) {
            uint32_t n = count < pipe->used ? count : pipe->used;
            for (uint32_t i = 0; i < n; ++i) {
                dst[i] = pipe->data[pipe->head];
                pipe->head = (pipe->head + 1) % PIPE_CAPACITY;
            }
            pipe->used -= n;
            task_wake_pipe();
            irq_restore(flags);
            return (int)n;
        }
        if (proc_signal_restart_current()) {
            irq_restore(flags);
            return (int)SYS_RESTART;
        }
        if (proc_signal_pending_current()) {
            irq_restore(flags);
            return KERR(EINTR);
        }
        if (!pipe->writers) {
            irq_restore(flags);
            return 0;
        }
        task_sleep_switch_locked(TASK_WAIT_PIPE, 0);
    }
}

int fd_pipe_write(task_file_t* file, const void* buf, uint32_t count) {
    if (!file || file->kind != FILE_PIPE ||
        file->pipe_end != PIPE_WRITE_END || !count) return KERR(EINVAL);

    kernel_pipe_t* pipe = file->pipe;
    const uint8_t* src = (const uint8_t*)buf;
    uint32_t done = 0;
    uint32_t flags = irq_save_disable();
    while (done < count) {
        if (!pipe->readers) {
            irq_restore(flags);
            return done ? (int)done : KERR(EPIPE);
        }
        if (pipe->used < PIPE_CAPACITY) {
            while (done < count && pipe->used < PIPE_CAPACITY) {
                pipe->data[pipe->tail] = src[done++];
                pipe->tail = (pipe->tail + 1) % PIPE_CAPACITY;
                pipe->used++;
            }
            task_wake_pipe();
            continue;
        }
        if (!done && proc_signal_restart_current()) {
            irq_restore(flags);
            return (int)SYS_RESTART;
        }
        if (proc_signal_pending_current()) {
            irq_restore(flags);
            return done ? (int)done : KERR(EINTR);
        }
        task_sleep_switch_locked(TASK_WAIT_PIPE, 0);
    }
    irq_restore(flags);
    return (int)done;
}
