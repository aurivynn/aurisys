#pragma once

#include <stdint.h>

#define BOOTINFO_MAGIC 0x41555249u // "AURI"

// e820 record, raw from INT15h AX=E820 (ecx=20, no attrs)
struct e820_entry {
	uint64_t base;		   // 0x00
	uint64_t len;		   // 0x08
	uint32_t type;		   // 0x10  1 = usable
} __attribute__((packed)); // 20 bytes

// stage2 fills this in @ 0x4000
struct bootinfo {
	uint32_t magic;		  // 0x00  BOOTINFO_MAGIC
	uint16_t vbe_mode;	  // 0x04
	uint32_t fb_addr;	  // 0x08  phys lfb
	uint32_t fb_pitch;	  // 0x0C  bytes/scanline
	uint16_t fb_width;	  // 0x10
	uint16_t fb_height;	  // 0x12
	uint16_t fb_bpp;	  // 0x14
	uint32_t mem_entries; // 0x18  e820 count
	e820_entry mem[32];	  // 0x1C
};