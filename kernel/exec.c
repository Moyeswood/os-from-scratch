#include "exec.h"
#include "fs.h"
#include "heap.h"
#include "paging.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "vm.h"

#define EXEC_PATH_BUF 64

void exec_basename(char* out, uint32_t cap, const char* path) {
    if (!out || cap == 0) return;
    const char* base = path ? path : "";
    for (const char* p = base; *p; ++p) {
        if (*p == '/') base = p + 1;
    }
    if (!*base) base = "exec";

    uint32_t n = 0;
    while (base[n] && n + 1 < cap) {
        out[n] = base[n];
        ++n;
    }
    out[n] = '\0';
}

static void make_bin_path(char* out, uint32_t cap, const char* name) {
    k_strcpy(out, "/bin/");
    uint32_t pos = k_strlen(out);
    uint32_t n = 0;
    while (name[n] && pos + 1 < cap) out[pos++] = name[n++];
    out[pos] = '\0';
}

void exec_install_builtins(void) {
    int bin = fs_lookup("/bin");
    if (bin < 0) bin = fs_create("/bin", FS_DIR);
    if (bin < 0) {
        serial_puts("[EXEC] install FAIL: cannot create /bin\n\r");
        return;
    }

    int installed = 0;
    for (int i = 0; i < user_prog_count; ++i) {
        const user_prog_t* p = &user_progs[i];
        char path[EXEC_PATH_BUF];
        make_bin_path(path, sizeof(path), p->name);

        int inum = fs_create(path, FS_FILE);
        if (inum < 0 || fs_write((uint32_t)inum, p->data, p->size, 0)
                       != (int)p->size) {
            if (inum >= 0) fs_remove(path);
            serial_puts("[EXEC] install FAIL path=");
            serial_puts(path);
            serial_puts("\n\r");
            continue;
        }
        ++installed;
    }

    serial_puts("[EXEC] installed ");
    serial_put_hex((uint32_t)installed);
    serial_puts("/");
    serial_put_hex((uint32_t)user_prog_count);
    serial_puts(" programs in /bin\n\r");
}

int exec_load_file(const char* path, uint8_t** image, uint32_t* size) {
    if (!path || !image || !size) return -1;
    *image = 0;
    *size = 0;

    int file_size = fs_size(path);
    if (file_size <= 0 ||
        (uint32_t)file_size > VM_IMAGE_LIMIT - VM_IMAGE_BASE) {
        serial_puts("[EXEC] invalid or missing executable: ");
        serial_puts(path);
        serial_puts("\n\r");
        return -1;
    }

    uint8_t* buffer = (uint8_t*)kmalloc((uint32_t)file_size);
    if (!buffer) {
        serial_puts("[EXEC] out of memory loading ");
        serial_puts(path);
        serial_puts("\n\r");
        return -1;
    }

    int got = fs_read((uint32_t)fs_lookup(path), buffer, 0, (uint32_t)file_size);
    if (got != file_size) {
        serial_puts("[EXEC] read failed: ");
        serial_puts(path);
        serial_puts("\n\r");
        kfree(buffer);
        return -1;
    }

    serial_puts("[EXEC] file path=");
    serial_puts(path);
    serial_puts(" bytes=");
    serial_put_hex((uint32_t)file_size);
    serial_puts("\n\r");
    *image = buffer;
    *size = (uint32_t)file_size;
    return 0;
}

int exec_replace_current(user_frame_t* frame, const char* path) {
    task_t* t = task_current();
    if (!t || !t->is_user || !frame) return -1;

    uint8_t* image = 0;
    uint32_t size = 0;
    if (exec_load_file(path, &image, &size) < 0) return -1;

    // 先在独立临时代码空间中完成全部可能失败的分配。旧地址空间
    // 在这期间保持不变，因此 exec 失败等价于一次普通返回 -1。
    task_t staged;
    k_memset(&staged, 0, sizeof(staged));
    staged.pid = t->pid;
    staged.is_user = 1;
    if (vm_init_image(&staged, image, size) < 0) {
        kfree(image);
        serial_puts("[EXEC] image setup failed, old process preserved\n\r");
        return -1;
    }

    uint32_t old_cr3 = t->cr3;
    exec_basename(t->name, sizeof(t->name), path);

    // 先切 CR3，再释放旧页目录。内核恒等映射在两边相同，
    // 所以当前内核栈和代码可以在切换过程中继续执行。
    __asm__ volatile("mov %0, %%cr3" : : "r"(staged.cr3) : "memory");
    t->cr3 = staged.cr3;
    t->image_end = staged.image_end;
    t->brk = staged.brk;
    t->mmap_brk = staged.mmap_brk;

    task_t old_vm;
    k_memset(&old_vm, 0, sizeof(old_vm));
    old_vm.cr3 = old_cr3;
    vm_destroy(&old_vm);
    kfree(image);

    // 丢弃旧 int 0x80 返回现场，直接 iret 到新镜像入口。
    frame->edi = 0;
    frame->esi = 0;
    frame->ebp = 0;
    frame->esp_orig = 0;
    frame->ebx = 0;
    frame->edx = 0;
    frame->ecx = 0;
    frame->eax = 0;
    frame->eip = VM_IMAGE_BASE;
    frame->cs = 0x1B;
    frame->eflags = 0x202;
    frame->esp_user = VM_STACK_TOP;
    frame->ss_user = 0x23;

    serial_puts("[EXEC] replace pid=");
    serial_put_hex(t->pid);
    serial_puts(" old_pd=");
    serial_put_hex(old_cr3);
    serial_puts(" new_pd=");
    serial_put_hex(t->cr3);
    serial_puts(" path=");
    serial_puts(path);
    serial_puts("\n\r");
    return 0;
}
