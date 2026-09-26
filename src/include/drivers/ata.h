#pragma once

#include <stdint.h>

namespace ata {

bool init(); // probe via identify
bool read_sector(uint32_t lba, void* dst);
bool write_sector(uint32_t lba, const void* src);
bool read_sectors(uint32_t lba, uint32_t n, void* dst);
bool write_sectors(uint32_t lba, uint32_t n, const void* src);

extern uint32_t sector_count;

} // namespace ata