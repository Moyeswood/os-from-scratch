#include "vm.h"
#include "memory.h"
#include "paging.h"
#include "serial.h"
#include "string.h"

#define PAGE_SIZE 4096u
#define ELF_PT_LOAD 1u

typedef struct __attribute__((packed)) {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} elf32_header_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} elf32_phdr_t;

static uint32_t align_down(uint32_t value) {
    return value & ~(PAGE_SIZE - 1u);
}

static uint32_t align_up(uint32_t value) {
    return (value + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
}

static uint32_t irq_save_disable(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void irq_restore(uint32_t flags) {
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
}

static int in_range(uint32_t value, uint32_t begin, uint32_t end) {
    return value >= begin && value < end;
}

static void put_hex(uint32_t value) {
    serial_put_hex(value);
}

static int map_zero_page(task_t* t, uint32_t virt) {
    uint32_t phys = alloc_page();
    if (!phys) return -1;

    k_memset((void*)phys, 0, PAGE_SIZE);
    if (paging_map_page(t->cr3, virt, phys,
                        PAGE_WRITE | PAGE_USER) < 0) {
        free_page(phys);
        return -1;
    }
    return 0;
}

static int ensure_user_page(task_t* t, uint32_t virt, uint32_t* phys) {
    uint32_t* pte = paging_user_pte(t->cr3, align_down(virt));
    if (pte && (*pte & PAGE_PRESENT)) {
        *phys = *pte & 0xFFFFF000u;
        return 0;
    }

    uint32_t page = alloc_page();
    if (!page) return -1;
    k_memset((void*)page, 0, PAGE_SIZE);
    if (paging_map_page(t->cr3, align_down(virt), page,
                        PAGE_WRITE | PAGE_USER) < 0) {
        free_page(page);
        return -1;
    }
    *phys = page;
    return 0;
}

static int write_user_bytes(task_t* t, uint32_t dst, const void* src,
                            uint32_t length) {
    const uint8_t* input = (const uint8_t*)src;
    while (length) {
        uint32_t page_dst = align_down(dst);
        uint32_t phys = 0;
        if (ensure_user_page(t, page_dst, &phys) < 0) return -1;
        uint32_t offset = dst - page_dst;
        uint32_t chunk = PAGE_SIZE - offset;
        if (chunk > length) chunk = length;
        k_memcpy((uint8_t*)phys + offset, input, chunk);
        dst += chunk;
        input += chunk;
        length -= chunk;
    }
    return 0;
}

int vm_init_image(task_t* t, const uint8_t* image, uint32_t size) {
    if (!t || !image || size > (VM_IMAGE_LIMIT - VM_IMAGE_BASE)) return -1;

    uint32_t pd = paging_create_user_dir();
    if (!pd) return -1;

    t->cr3 = pd;
    t->image_end = VM_IMAGE_BASE + align_up(size);
    t->brk = VM_HEAP_BASE;
    t->mmap_brk = VM_MMAP_BASE;

    for (uint32_t off = 0; off < align_up(size); off += PAGE_SIZE) {
        uint32_t phys = alloc_page();
        if (!phys) {
            vm_destroy(t);
            return -1;
        }
        k_memset((void*)phys, 0, PAGE_SIZE);
        uint32_t remain = size > off ? size - off : 0;
        if (remain > PAGE_SIZE) remain = PAGE_SIZE;
        if (remain) k_memcpy((void*)phys, image + off, remain);
        if (paging_map_page(pd, VM_IMAGE_BASE + off, phys,
                            PAGE_WRITE | PAGE_USER) < 0) {
            free_page(phys);
            vm_destroy(t);
            return -1;
        }
    }

    serial_puts("[VM] image pid=");
    put_hex(t->pid);
    serial_puts(" bytes=");
    put_hex(size);
    serial_puts(" mapped=");
    put_hex(align_up(size) / PAGE_SIZE);
    serial_puts(" pages\n\r");
    return 0;
}

static int vm_init_elf(task_t* t, const uint8_t* image, uint32_t size,
                       uint32_t* entry) {
    if (size < sizeof(elf32_header_t)) return -1;
    const elf32_header_t* eh = (const elf32_header_t*)image;
    if (eh->ident[0] != 0x7F || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L' || eh->ident[3] != 'F' ||
        eh->ident[4] != 1 || eh->ident[5] != 1 ||
        eh->type != 2 || eh->machine != 3 || eh->version != 1 ||
        eh->ehsize < sizeof(elf32_header_t) ||
        eh->phentsize < sizeof(elf32_phdr_t) ||
        eh->phnum == 0 || eh->phnum > 16) {
        serial_puts("[ELF] reject: unsupported header\n\r");
        return -1;
    }

    uint64_t ph_end = (uint64_t)eh->phoff +
                      (uint64_t)eh->phnum * eh->phentsize;
    if (ph_end > size) {
        serial_puts("[ELF] reject: program headers outside file\n\r");
        return -1;
    }

    uint32_t pd = paging_create_user_dir();
    if (!pd) return -1;
    t->cr3 = pd;
    t->brk = VM_HEAP_BASE;
    t->mmap_brk = VM_MMAP_BASE;
    t->image_end = VM_IMAGE_BASE;

    int segments = 0;
    for (uint32_t i = 0; i < eh->phnum; ++i) {
        const elf32_phdr_t* ph = (const elf32_phdr_t*)
            (image + eh->phoff + i * eh->phentsize);
        if (ph->type != ELF_PT_LOAD) continue;
        ++segments;

        uint64_t vend = (uint64_t)ph->vaddr + ph->memsz;
        uint64_t fend = (uint64_t)ph->offset + ph->filesz;
        if (ph->filesz > ph->memsz || fend > size ||
            ph->vaddr < VM_IMAGE_BASE || vend > VM_HEAP_BASE ||
            vend < ph->vaddr) {
            serial_puts("[ELF] reject: invalid PT_LOAD bounds\n\r");
            vm_destroy(t);
            return -1;
        }

        for (uint32_t addr = align_down(ph->vaddr);
             addr < align_up((uint32_t)vend); addr += PAGE_SIZE) {
            uint32_t phys = 0;
            if (ensure_user_page(t, addr, &phys) < 0) {
                serial_puts("[ELF] reject: segment allocation failed\n\r");
                vm_destroy(t);
                return -1;
            }
        }
        if (ph->filesz &&
            write_user_bytes(t, ph->vaddr, image + ph->offset,
                             ph->filesz) < 0) {
            vm_destroy(t);
            return -1;
        }
        if (!(ph->flags & 2)) {
            for (uint32_t addr = align_down(ph->vaddr);
                 addr < align_up((uint32_t)vend); addr += PAGE_SIZE) {
                uint32_t* pte = paging_user_pte(t->cr3, addr);
                if (pte) *pte &= ~PAGE_WRITE;
            }
        }
        if ((uint32_t)vend > t->image_end) t->image_end = (uint32_t)vend;
    }

    if (!segments || eh->entry < VM_IMAGE_BASE ||
        eh->entry >= t->image_end ||
        !paging_translate(t->cr3, eh->entry)) {
        serial_puts("[ELF] reject: invalid entry or no loadable segments\n\r");
        vm_destroy(t);
        return -1;
    }

    *entry = eh->entry;
    serial_puts("[ELF] load pid=");
    put_hex(t->pid);
    serial_puts(" entry=");
    put_hex(eh->entry);
    serial_puts(" segments=");
    put_hex((uint32_t)segments);
    serial_puts(" image_end=");
    put_hex(t->image_end);
    serial_puts("\n\r");
    return 0;
}

int vm_init_program(task_t* t, const uint8_t* image, uint32_t size,
                    uint32_t* entry) {
    if (!t || !image || !entry) return -1;
    if (size >= 4 && image[0] == 0x7F && image[1] == 'E' &&
        image[2] == 'L' && image[3] == 'F')
        return vm_init_elf(t, image, size, entry);

    if (vm_init_image(t, image, size) < 0) return -1;
    *entry = VM_IMAGE_BASE;
    return 0;
}

int vm_build_initial_stack(task_t* t, int argc, const char* const* argv,
                           int envc, const char* const* envp,
                           uint32_t* initial_esp) {
    if (!t || !initial_esp || argc < 0 || envc < 0 ||
        argc > 16 || envc > 8) return -1;

    uint32_t cursor = VM_STACK_TOP;
    uint32_t argv_user[16];
    uint32_t envp_user[8];

    for (int i = 0; i < argc; ++i) {
        if (!argv || !argv[i]) return -1;
        uint32_t len = k_strlen(argv[i]) + 1;
        if (len > cursor - VM_STACK_BOTTOM) return -1;
        cursor -= len;
        if (write_user_bytes(t, cursor, argv[i], len) < 0) return -1;
        argv_user[i] = cursor;
    }
    for (int i = 0; i < envc; ++i) {
        if (!envp || !envp[i]) return -1;
        uint32_t len = k_strlen(envp[i]) + 1;
        if (len > cursor - VM_STACK_BOTTOM) return -1;
        cursor -= len;
        if (write_user_bytes(t, cursor, envp[i], len) < 0) return -1;
        envp_user[i] = cursor;
    }

    uint32_t ptr_bytes = (uint32_t)(argc + envc + 2) * 4u;
    if (cursor < VM_STACK_BOTTOM + PAGE_SIZE + ptr_bytes + 16u) return -1;
    cursor = align_down(cursor - ptr_bytes);
    uint32_t argv_ptrs = cursor;
    for (int i = 0; i < argc; ++i)
        if (write_user_bytes(t, cursor + (uint32_t)i * 4,
                             &argv_user[i], 4) < 0) return -1;
    uint32_t zero = 0;
    if (write_user_bytes(t, cursor + (uint32_t)argc * 4, &zero, 4) < 0)
        return -1;
    uint32_t envp_ptrs = cursor + (uint32_t)(argc + 1) * 4;
    for (int i = 0; i < envc; ++i)
        if (write_user_bytes(t, envp_ptrs + (uint32_t)i * 4,
                             &envp_user[i], 4) < 0) return -1;
    if (write_user_bytes(t, envp_ptrs + (uint32_t)envc * 4, &zero, 4) < 0)
        return -1;

    cursor = align_down(cursor - 16u);
    uint32_t words[4] = { 0, (uint32_t)argc, argv_ptrs, envp_ptrs };
    if (write_user_bytes(t, cursor, words, sizeof(words)) < 0) return -1;
    *initial_esp = cursor;
    return 0;
}

static void destroy_dir(uint32_t pd) {
    if (!pd) return;
    uint32_t* dir = (uint32_t*)pd;

    uint32_t flags = irq_save_disable();
    for (uint32_t pdi = 8; pdi < 30; ++pdi) {
        if (!(dir[pdi] & PAGE_PRESENT)) continue;
        uint32_t pt_phys = dir[pdi] & 0xFFFFF000u;
        uint32_t* pt = (uint32_t*)pt_phys;
        for (uint32_t i = 0; i < 1024; ++i) {
            if (pt[i] & PAGE_PRESENT)
                page_put(pt[i] & 0xFFFFF000u);
        }
        free_page(pt_phys);
        dir[pdi] = 0;
    }
    free_page(pd);
    irq_restore(flags);
}

void vm_destroy(task_t* t) {
    if (!t) return;
    uint32_t pd = t->cr3;
    t->cr3 = 0;
    t->brk = 0;
    t->mmap_brk = 0;
    t->image_end = 0;
    destroy_dir(pd);
}

int vm_clone(task_t* parent, task_t* child) {
    uint32_t child_pd = paging_create_user_dir();
    if (!child_pd) return -1;

    child->cr3 = child_pd;
    child->brk = parent->brk;
    child->mmap_brk = parent->mmap_brk;
    child->image_end = parent->image_end;

    uint32_t* parent_dir = (uint32_t*)parent->cr3;
    uint32_t* child_dir = (uint32_t*)child_pd;

    uint32_t irq_flags = irq_save_disable();
    for (uint32_t pdi = 8; pdi < 30; ++pdi) {
        if (!(parent_dir[pdi] & PAGE_PRESENT)) continue;

        uint32_t child_pt_phys = alloc_page();
        if (!child_pt_phys) {
            irq_restore(irq_flags);
            vm_destroy(child);
            return -1;
        }
        uint32_t* parent_pt =
            (uint32_t*)(parent_dir[pdi] & 0xFFFFF000u);
        uint32_t* child_pt = (uint32_t*)child_pt_phys;
        k_memset(child_pt, 0, PAGE_SIZE);

        for (uint32_t i = 0; i < 1024; ++i) {
            if (!(parent_pt[i] & PAGE_PRESENT)) continue;
            uint32_t phys = parent_pt[i] & 0xFFFFF000u;
            page_get(phys);
            if (parent_pt[i] & PAGE_WRITE)
                child_pt[i] = (parent_pt[i] & ~PAGE_WRITE) | PAGE_COW;
            else
                child_pt[i] = parent_pt[i];
        }
        child_dir[pdi] = child_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    // 只有子页表完整建立后才冻结父页，失败路径无需回滚父 PTE。
    for (uint32_t pdi = 8; pdi < 30; ++pdi) {
        if (!(parent_dir[pdi] & PAGE_PRESENT)) continue;
        uint32_t* parent_pt =
            (uint32_t*)(parent_dir[pdi] & 0xFFFFF000u);
        for (uint32_t i = 0; i < 1024; ++i) {
            if ((parent_pt[i] & PAGE_PRESENT) &&
                (parent_pt[i] & PAGE_WRITE))
                parent_pt[i] = (parent_pt[i] & ~PAGE_WRITE) | PAGE_COW;
        }
    }
    paging_flush_all();
    irq_restore(irq_flags);

    serial_puts("[VM] fork pid=");
    put_hex(parent->pid);
    serial_puts(" child=");
    put_hex(child->pid);
    serial_puts(" shared_pages=");
    put_hex(get_shared_pages());
    serial_puts(" (COW)\n\r");
    return 0;
}

static int cowed_page(task_t* t, uint32_t addr, uint32_t error) {
    if (!(error & 2)) return 0;
    uint32_t* pte = paging_user_pte(t->cr3, addr);
    if (!pte || !(*pte & PAGE_PRESENT) || !(*pte & PAGE_COW)) return 0;

    uint32_t old_phys = *pte & 0xFFFFF000u;
    if (page_refcount(old_phys) > 1) {
        uint32_t new_phys = alloc_page();
        if (!new_phys) return 0;
        k_memcpy((void*)new_phys, (void*)old_phys, PAGE_SIZE);
        *pte = new_phys | ((*pte) & 0xFFFu) | PAGE_WRITE;
        *pte &= ~PAGE_COW;
        page_put(old_phys);
        serial_puts("[VM] COW pid=");
        put_hex(t->pid);
        serial_puts(" old=");
        put_hex(old_phys);
        serial_puts(" new=");
        put_hex(new_phys);
        serial_puts("\n\r");
    } else {
        // 对方已经退出，最后一个引用无需复制，恢复可写即可。
        *pte = (*pte | PAGE_WRITE) & ~PAGE_COW;
        serial_puts("[VM] COW pid=");
        put_hex(t->pid);
        serial_puts(" reuse=unique\n\r");
    }
    paging_invalidate_page(align_down(addr));
    return 1;
}

/* 修复(2026-11-12): 从 vm_handle_page_fault 抽出的按需映射核心。
 * 对用户空间内合法的未映射地址建立零页 (堆 brk 内/mmap/栈, 栈守卫除外)。
 * 内核侧 copy_to/from_user 语义: 直接读写用户缓冲前必须先 fault-in。 */
int vm_demand_fault(task_t* t, uint32_t addr) {
    if (!t || !t->is_user) return 0;

    if (!in_range(addr, USER_BASE, VM_STACK_TOP)) {
        serial_puts("[VM] reject pid=");
        put_hex(t->pid);
        serial_puts(" addr=");
        put_hex(addr);
        serial_puts(" outside user space\n\r");
        return 0;
    }
    if (in_range(addr, VM_STACK_GUARD, VM_STACK_BOTTOM)) {
        serial_puts("[VM] reject pid=");
        put_hex(t->pid);
        serial_puts(" addr=");
        put_hex(addr);
        serial_puts(" stack guard page\n\r");
        return 0;
    }

    int allowed =
        in_range(addr, VM_HEAP_BASE, align_up(t->brk)) ||
        in_range(addr, VM_MMAP_BASE, align_up(t->mmap_brk)) ||
        in_range(addr, VM_STACK_BOTTOM, VM_STACK_TOP);
    if (!allowed) {
        serial_puts("[VM] reject pid=");
        put_hex(t->pid);
        serial_puts(" addr=");
        put_hex(addr);
        serial_puts(" outside heap/mmap/stack\n\r");
        return 0;
    }

    if (map_zero_page(t, align_down(addr)) < 0) {
        serial_puts("[VM] demand alloc failed pid=");
        put_hex(t->pid);
        serial_puts(" addr=");
        put_hex(addr);
        serial_puts("\n\r");
        return 0;
    }

    const char* area = addr < VM_HEAP_LIMIT ? "heap" :
                       addr < VM_MMAP_LIMIT ? "mmap" : "stack";
    serial_puts("[VM] demand pid=");
    put_hex(t->pid);
    serial_puts(" area=");
    serial_puts(area);
    serial_puts(" addr=");
    put_hex(align_down(addr));
    serial_puts("\n\r");
    return 1;
}

/* 修复(2026-11-12): 内核直接读写用户缓冲前, 把 [addr, addr+length)
 * 范围内缺失的页全部按需建立。任一页非法/失败返回 0 (调用方报 EFAULT)。
 * 注意: 已映射但 COW 共享的页不在这里处理 (内核写会绕过 COW 断点,
 * 属后续改进项; 当前 fork 后子进程首次触碰共享页仍走 #PF 的 cowed_page)。 */
int vm_fault_user_range(task_t* t, uint32_t addr, uint32_t length) {
    if (!t || !t->is_user || addr < USER_BASE || addr >= VM_STACK_TOP)
        return 0;
    if (length == 0) return 1;
    if (length > VM_STACK_TOP - addr) return 0;

    uint32_t last = addr + length - 1u;
    for (uint32_t page = align_down(addr);
         page <= align_down(last); page += PAGE_SIZE) {
        if (paging_translate(t->cr3, page)) continue;   /* 已映射, 跳过 */
        if (!vm_demand_fault(t, page)) return 0;
    }
    return 1;
}

int vm_handle_page_fault(task_t* t, uint32_t addr, uint32_t error) {
    if (!t || !t->is_user || !(error & 4)) return 0;

    if (error & PAGE_PRESENT)
        return cowed_page(t, addr, error);

    return vm_demand_fault(t, addr);
}

uint32_t vm_brk(task_t* t, uint32_t new_break) {
    if (!t || !t->is_user || new_break < VM_HEAP_BASE ||
        new_break > VM_HEAP_LIMIT)
        return 0xFFFFFFFFu;

    uint32_t old_break = t->brk;
    uint32_t flags = irq_save_disable();
    if (new_break < old_break) {
        for (uint32_t addr = align_up(new_break);
             addr < align_up(old_break); addr += PAGE_SIZE) {
            uint32_t phys = paging_translate(t->cr3, addr);
            if (phys) {
                paging_unmap_page(t->cr3, addr);
                page_put(align_down(phys));
                paging_invalidate_page(addr);
            }
        }
    }
    t->brk = new_break;
    irq_restore(flags);
    return old_break;
}

uint32_t vm_mmap(task_t* t, uint32_t length) {
    if (!t || !t->is_user || length == 0) return 0xFFFFFFFFu;
    uint32_t bytes = align_up(length);
    if (bytes < length || bytes > VM_MMAP_LIMIT - t->mmap_brk)
        return 0xFFFFFFFFu;

    uint32_t start = t->mmap_brk;
    t->mmap_brk += bytes;
    return start;
}

int vm_user_range_ok(task_t* t, uint32_t addr, uint32_t length) {
    if (!t || !t->is_user || addr < USER_BASE || addr >= VM_STACK_TOP)
        return 0;
    if (length == 0) return 1;
    if (length > VM_STACK_TOP - addr) return 0;

    uint32_t last = addr + length - 1u;
    for (uint32_t page = align_down(addr);
         page <= align_down(last); page += PAGE_SIZE) {
        if (!paging_translate(t->cr3, page)) return 0;
    }
    return 1;
}

int vm_read_user(task_t* t, uint32_t addr, void* destination,
                 uint32_t length) {
    if (!t || !t->is_user || (!destination && length)) return -1;
    uint8_t* output = (uint8_t*)destination;

    while (length) {
        uint32_t page_addr = align_down(addr);
        uint32_t phys = paging_translate(t->cr3, page_addr);
        if (!phys) return -1;
        uint32_t offset = addr - page_addr;
        uint32_t chunk = PAGE_SIZE - offset;
        if (chunk > length) chunk = length;
        k_memcpy(output, (const void*)(phys + offset), chunk);
        addr += chunk;
        output += chunk;
        length -= chunk;
    }
    return 0;
}

int vm_write_user(task_t* t, uint32_t addr, const void* source,
                  uint32_t length) {
    if (!t || !t->is_user || (!source && length)) return -1;
    const uint8_t* input = (const uint8_t*)source;

    while (length) {
        uint32_t page_addr = align_down(addr);
        uint32_t* pte = paging_user_pte(t->cr3, page_addr);
        if (!pte || !(*pte & PAGE_PRESENT)) return -1;

        if (*pte & PAGE_COW) {
            uint32_t old_phys = *pte & 0xFFFFF000u;
            if (page_refcount(old_phys) > 1) {
                uint32_t new_phys = alloc_page();
                if (!new_phys) return -1;
                k_memcpy((void*)new_phys, (void*)old_phys, PAGE_SIZE);
                *pte = new_phys | ((*pte) & 0xFFFu) | PAGE_WRITE;
                *pte &= ~PAGE_COW;
                page_put(old_phys);
            } else {
                *pte = (*pte | PAGE_WRITE) & ~PAGE_COW;
            }
            paging_invalidate_page(page_addr);
        }

        uint32_t phys = *pte & 0xFFFFF000u;
        uint32_t offset = addr - page_addr;
        uint32_t chunk = PAGE_SIZE - offset;
        if (chunk > length) chunk = length;
        k_memcpy((uint8_t*)phys + offset, input, chunk);
        addr += chunk;
        input += chunk;
        length -= chunk;
    }
    return 0;
}
