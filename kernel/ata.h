#ifndef ATA_H
#define ATA_H

#include "types.h"

#define ATA_SECTOR_SIZE 512

// Primary IDE master (legacy PIO, 28-bit LBA). Sector writes are complete
// when this function returns; call ata_flush() after a batch to force the
// device cache to stable storage.
int ata_init(void);
int ata_present(void);
int ata_read_sector(uint32_t lba, void* buf);
int ata_write_sector(uint32_t lba, const void* buf);
int ata_flush(void);

#endif
