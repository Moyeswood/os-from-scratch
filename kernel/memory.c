#include "memory.h"
#include "serial.h"
#include "string.h"

#define PAGE_SIZE          4096
#define BITMAP_BASE        0x100000u
#define RESERVED_BITMAP    (BITMAP_BASE + 0x1000u)
#define FIRST_MANAGED_ADDR 0x00100000u
#define PAGE_TABLES_BASE   0x01000000u
#define PAGE_TABLES_BYTES  0x0000B000u
#define MAX_MANAGED_PAGES  (MEMORY_IDENTITY_LIMIT / PAGE_SIZE)

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

static uint8_t* bitmap;
static uint8_t* reserved_bitmap;
static uint32_t bitmap_size;
static uint32_t total_pages;
static uint32_t usable_entries;
static uint64_t total_bytes;
static uint64_t usable_bytes;
static memory_map_entry_t e820_entries[MEMORY_MAP_MAX];
static uint32_t e820_count;
static uint8_t page_refs[MAX_MANAGED_PAGES];

static uint32_t irq_save_disable() {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static void irq_restore(uint32_t flags) {
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
}

static int bit_get(const uint8_t* map, uint32_t page) {
    return (map[page / 8] >> (page % 8)) & 1;
}

static void bit_set(uint8_t* map, uint32_t page) {
    map[page / 8] |= (uint8_t)(1u << (page % 8));
}

static void bit_clear(uint8_t* map, uint32_t page) {
    map[page / 8] &= (uint8_t)~(1u << (page % 8));
}

static int valid_managed_page(uint32_t addr) {
    if (addr & (PAGE_SIZE - 1)) return 0;
    uint32_t page = addr / PAGE_SIZE;
    return page < total_pages && page < MAX_MANAGED_PAGES
        && bit_get(bitmap, page);
}

static uint64_t entry_end(const memory_map_entry_t* e) {
    uint64_t end = e->base + e->length;
    return end < e->base ? 0xFFFFFFFFFFFFFFFFull : end;
}

static void load_boot_map() {
    uint32_t count = *(volatile uint16_t*)MEMORY_MAP_COUNT_ADDR;
    if (count > MEMORY_MAP_MAX) count = MEMORY_MAP_MAX;
    e820_count = count;
    if (count) {
        k_memcpy(e820_entries, (const void*)MEMORY_MAP_ADDR,
                 count * sizeof(memory_map_entry_t));
    }

    // 极老 BIOS 没有 E820 时，boot.asm 会写 0x88 回退项。
    if (!e820_count) {
        uint16_t ext_kb = *(volatile uint16_t*)0x500;
        e820_entries[0].base = FIRST_MANAGED_ADDR;
        e820_entries[0].length = (uint64_t)ext_kb * 1024;
        e820_entries[0].type = MEMORY_TYPE_USABLE;
        e820_entries[0].acpi_attributes = 0;
        e820_count = 1;
    }
}

static void reserve_range(uint32_t addr, uint32_t bytes) {
    memory_reserve(addr, bytes);
}

void memory_init() {
    serial_puts("\n\r[Memory] Parsing BIOS E820 map...\n\r");
    load_boot_map();

    uint64_t max_end = 0;
    for (uint32_t i = 0; i < e820_count; ++i) {
        memory_map_entry_t* e = &e820_entries[i];
        uint64_t end = entry_end(e);
        total_bytes += e->length;
        if (end > max_end) max_end = end;
        if (e->type == MEMORY_TYPE_USABLE) {
            usable_entries++;
            usable_bytes += e->length;
        }
    }

    if (max_end > MEMORY_IDENTITY_LIMIT)
        max_end = MEMORY_IDENTITY_LIMIT;
    if (max_end < FIRST_MANAGED_ADDR + PAGE_SIZE) {
        serial_puts("[Memory] FATAL: no usable memory under identity limit\n\r");
        for (;;) __asm__ volatile("cli; hlt");
    }

    total_pages = (uint32_t)(max_end / PAGE_SIZE);
    bitmap_size = (total_pages + 7) / 8;
    bitmap = (uint8_t*)BITMAP_BASE;
    reserved_bitmap = (uint8_t*)RESERVED_BITMAP;

    // 默认所有页都不可分配；E820 usable 区域稍后再打开。
    for (uint32_t i = 0; i < bitmap_size; ++i) {
        bitmap[i] = 0xFF;
        reserved_bitmap[i] = 0xFF;
    }

    for (uint32_t i = 0; i < e820_count; ++i) {
        memory_map_entry_t* e = &e820_entries[i];
        if (e->type != MEMORY_TYPE_USABLE) continue;
        uint64_t start64 = e->base;
        uint64_t end64 = entry_end(e);
        if (start64 < FIRST_MANAGED_ADDR) start64 = FIRST_MANAGED_ADDR;
        if (end64 > MEMORY_IDENTITY_LIMIT) end64 = MEMORY_IDENTITY_LIMIT;
        if (start64 >= end64) continue;
        uint32_t first = (uint32_t)((start64 + PAGE_SIZE - 1) / PAGE_SIZE);
        uint32_t last = (uint32_t)(end64 / PAGE_SIZE);
        for (uint32_t page = first; page < last; ++page) {
            bit_clear(bitmap, page);
            bit_clear(reserved_bitmap, page);
        }
    }

    // 内核镜像、分配位图、页表所在物理页永远不交给调用者。
    reserve_range((uint32_t)_kernel_start,
                  (uint32_t)(_kernel_end - _kernel_start));
    reserve_range(BITMAP_BASE, PAGE_SIZE * 2);
    reserve_range(PAGE_TABLES_BASE, PAGE_TABLES_BYTES);

    serial_puts("[MEM] E820 entries=");
    serial_put_hex(e820_count);
    serial_puts(" usable=");
    serial_put_hex(usable_entries);
    serial_puts(" managed_pages=");
    serial_put_hex(total_pages);
    serial_puts("\n\r");
    serial_puts("[MEM] allocator source: E820, identity limit 32MB\n\r");
}

static uint32_t alloc_pages_locked(uint32_t n) {
    if (!n) return 0;
    uint32_t run_start = 0;
    uint32_t run = 0;
    for (uint32_t page = 0; page < total_pages; ++page) {
        if (!bit_get(bitmap, page)) {
            if (!run) run_start = page;
            ++run;
            if (run == n) {
                for (uint32_t p = run_start; p < run_start + n; ++p) {
                    bit_set(bitmap, p);
                    page_refs[p] = 1;
                }
                return run_start * PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }
    return 0;
}

uint32_t alloc_pages(uint32_t n) {
    uint32_t flags = irq_save_disable();
    uint32_t addr = alloc_pages_locked(n);
    irq_restore(flags);
    return addr;
}

uint32_t alloc_page() {
    return alloc_pages(1);
}

static void free_page_locked(uint32_t addr) {
    if (addr & (PAGE_SIZE - 1)) return;
    uint32_t page = addr / PAGE_SIZE;
    if (page >= total_pages || bit_get(reserved_bitmap, page)) return;
    bit_clear(bitmap, page);
    if (page < MAX_MANAGED_PAGES) page_refs[page] = 0;
}

void free_page(uint32_t addr) {
    uint32_t flags = irq_save_disable();
    free_page_locked(addr);
    irq_restore(flags);
}

void free_pages(uint32_t addr, uint32_t n) {
    uint32_t flags = irq_save_disable();
    for (uint32_t i = 0; i < n; ++i)
        free_page_locked(addr + i * PAGE_SIZE);
    irq_restore(flags);
}

void page_get(uint32_t addr) {
    uint32_t flags = irq_save_disable();
    if (valid_managed_page(addr)) {
        uint32_t page = addr / PAGE_SIZE;
        if (page_refs[page] != 0xFF) page_refs[page]++;
    }
    irq_restore(flags);
}

void page_put(uint32_t addr) {
    uint32_t flags = irq_save_disable();
    if (valid_managed_page(addr)) {
        uint32_t page = addr / PAGE_SIZE;
        if (page_refs[page] > 1) {
            page_refs[page]--;
        } else {
            page_refs[page] = 0;
            free_page_locked(addr);
        }
    }
    irq_restore(flags);
}

uint32_t page_refcount(uint32_t addr) {
    uint32_t flags = irq_save_disable();
    uint32_t count = 0;
    if (valid_managed_page(addr)) count = page_refs[addr / PAGE_SIZE];
    irq_restore(flags);
    return count;
}

void memory_reserve(uint32_t addr, uint32_t bytes) {
    if (!bytes) return;
    uint32_t flags = irq_save_disable();
    uint32_t first = addr / PAGE_SIZE;
    uint32_t last = (addr + bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (last > total_pages) last = total_pages;
    for (uint32_t page = first; page < last; ++page) {
        bit_set(bitmap, page);
        bit_set(reserved_bitmap, page);
    }
    irq_restore(flags);
}

uint32_t get_total_pages() { return total_pages; }

uint32_t get_used_pages() {
    uint32_t used = 0;
    for (uint32_t page = 0; page < total_pages; ++page)
        used += bit_get(bitmap, page) != 0;
    return used;
}

uint32_t get_free_pages() {
    return total_pages - get_used_pages();
}

uint32_t get_shared_pages() {
    uint32_t shared = 0;
    for (uint32_t page = 0; page < total_pages && page < MAX_MANAGED_PAGES; ++page) {
        if (bit_get(bitmap, page) && page_refs[page] > 1) shared++;
    }
    return shared;
}

uint32_t memory_map_count() { return e820_count; }
const memory_map_entry_t* memory_map_entry(uint32_t index) {
    return index < e820_count ? &e820_entries[index] : 0;
}
uint32_t memory_map_usable_entries() { return usable_entries; }
uint64_t memory_map_total_bytes() { return total_bytes; }
uint64_t memory_map_usable_bytes() { return usable_bytes; }
uint32_t memory_identity_limit() { return MEMORY_IDENTITY_LIMIT; }

int memory_selftest() {
    int ok = 1;
    uint32_t first = alloc_page();
    if (!first || first < FIRST_MANAGED_ADDR) ok = 0;
    if (first) free_page(first);
    uint32_t second = alloc_page();
    if (!second || second != first) ok = 0;
    if (second) free_page(second);

    // 页表所在页被标记为 reserved，错误 free 必须无效。
    uint32_t table_page = PAGE_TABLES_BASE / PAGE_SIZE;
    int before = bit_get(bitmap, table_page);
    free_page(PAGE_TABLES_BASE);
    int after = bit_get(bitmap, table_page);
    if (!before || !after) ok = 0;

    serial_puts("[MEM] selftest ");
    serial_puts(ok ? "PASS\n\r" : "FAIL\n\r");
    return ok;
}
