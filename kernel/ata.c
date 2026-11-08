#include "ata.h"
#include "io.h"
#include "serial.h"
#include "types.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7
#define ATA_CONTROL     0x3F6

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_FLUSH   0xE7
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_STATUS_ERR  0x01
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_DF   0x20
#define ATA_STATUS_BSY  0x80

#define ATA_MAX_LBA     0x0FFFFFFFu
#define ATA_TIMEOUT     1000000u

static int ready;

static uint32_t irq_save_disable(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uint32_t flags) {
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
}

static void ata_delay_400ns(void) {
    // Reading the alternate status four times gives the mandated settling gap.
    (void)inb(ATA_CONTROL);
    (void)inb(ATA_CONTROL);
    (void)inb(ATA_CONTROL);
    (void)inb(ATA_CONTROL);
}

static int ata_wait_not_busy(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (!(status & ATA_STATUS_BSY)) return status;
    }
    return -1;
}

static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return -1;
        if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ))
            return 0;
    }
    return -1;
}

static void ata_select(uint32_t lba) {
    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay_400ns();
}

static void ata_set_lba(uint32_t lba) {
    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
}

static int ata_flush_locked(void) {
    if (ata_wait_not_busy() < 0) return -1;
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    if (ata_wait_not_busy() < 0) return -1;
    return (inb(ATA_STATUS) & (ATA_STATUS_ERR | ATA_STATUS_DF)) ? -1 : 0;
}

int ata_init(void) {
    uint16_t identify[256];
    uint32_t flags = irq_save_disable();

    ready = 0;
    outb(ATA_CONTROL, 0);
    outb(ATA_DRIVE, 0xA0);
    ata_delay_400ns();
    outb(ATA_SECTOR_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay_400ns();

    uint8_t status = inb(ATA_STATUS);
    if (!status) {
        irq_restore(flags);
        serial_puts("[ATA] primary master not present\n\r");
        return -1;
    }
    if (ata_wait_not_busy() < 0 || inb(ATA_LBA_MID) || inb(ATA_LBA_HIGH)) {
        irq_restore(flags);
        serial_puts("[ATA] primary master is not an ATA disk\n\r");
        return -1;
    }
    if (ata_wait_drq() < 0) {
        irq_restore(flags);
        serial_puts("[ATA] identify failed\n\r");
        return -1;
    }

    insw(ATA_DATA, identify, 256);
    if (!(identify[49] & 0x0200)) {
        irq_restore(flags);
        serial_puts("[ATA] disk does not support LBA\n\r");
        return -1;
    }

    ready = 1;
    serial_puts("[ATA] primary master ready, LBA28\n\r");
    irq_restore(flags);
    return 0;
}

int ata_present(void) {
    return ready;
}

int ata_read_sector(uint32_t lba, void* buf) {
    if (!ready || !buf || lba > ATA_MAX_LBA) return -1;
    uint32_t flags = irq_save_disable();

    if (ata_wait_not_busy() < 0) {
        irq_restore(flags);
        return -1;
    }
    ata_select(lba);
    ata_set_lba(lba);
    outb(ATA_COMMAND, ATA_CMD_READ);
    if (ata_wait_drq() < 0) {
        irq_restore(flags);
        return -1;
    }
    insw(ATA_DATA, buf, ATA_SECTOR_SIZE / 2);
    ata_delay_400ns();

    int failed = (inb(ATA_STATUS) & (ATA_STATUS_ERR | ATA_STATUS_DF)) != 0;
    irq_restore(flags);
    return failed ? -1 : 0;
}

int ata_write_sector(uint32_t lba, const void* buf) {
    if (!ready || !buf || lba > ATA_MAX_LBA) return -1;
    uint32_t flags = irq_save_disable();

    if (ata_wait_not_busy() < 0) {
        irq_restore(flags);
        return -1;
    }
    ata_select(lba);
    ata_set_lba(lba);
    outb(ATA_COMMAND, ATA_CMD_WRITE);
    if (ata_wait_drq() < 0) {
        irq_restore(flags);
        return -1;
    }
    outsw(ATA_DATA, buf, ATA_SECTOR_SIZE / 2);
    ata_delay_400ns();

    int result = (ata_wait_not_busy() < 0) ? -1 : 0;
    if (!result && (inb(ATA_STATUS) & (ATA_STATUS_ERR | ATA_STATUS_DF)))
        result = -1;
    irq_restore(flags);
    return result;
}

int ata_flush(void) {
    if (!ready) return -1;
    uint32_t flags = irq_save_disable();
    int result = ata_flush_locked();
    irq_restore(flags);
    return result;
}
