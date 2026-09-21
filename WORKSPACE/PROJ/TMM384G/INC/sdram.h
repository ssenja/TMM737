#ifndef SDRAM_H
#define SDRAM_H

#include "gd32h73x_75x.h"
#include <stdint.h>

#define SDRAM_BASE_ADDR        ((uint32_t)0xC0000000U)
#define SDRAM_SIZE_BYTES      (32U * 1024U * 1024U)
#define SDRAM_END_ADDR        (SDRAM_BASE_ADDR + SDRAM_SIZE_BYTES)

void sdram_init(void);
void sdram_write16(uint32_t address, uint16_t data);
uint16_t sdram_read16(uint32_t address);
void sdram_write_buffer(uint32_t address, const uint16_t *buffer, uint32_t count);
void sdram_read_buffer(uint32_t address, uint16_t *buffer, uint32_t count);

/* Returns 0 on success. Non-zero is the failing byte address + 1. */
uint32_t sdram_memory_test(void);

#endif
