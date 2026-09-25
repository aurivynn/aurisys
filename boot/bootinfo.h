#pragma once
#include <stdint.h>

#define BOOTINFO_MAGIC 0x41555249u // "AURI"

struct e820_entry {
	uint64_t base;
	uint32_t len;
	uint32_t type;
};

struct bootinfo {
	uint32_t magic;		  // 0x00  BOOTINFO_MAGIC
	uint16_t vbe_mode;	  // 0x04
	uint32_t fb_addr;	  // 0x08  physical linear framebuffer
	uint32_t fb_pitch;	  // 0x0C  bytes per scanline (1280*4 = 5120)
	uint16_t fb_width;	  // 0x10  1280
	uint16_t fb_height;	  // 0x12  960
	uint16_t fb_bpp;	  // 0x14  32
	uint32_t mem_entries; // 0x18  count of E820 entries below
	e820_entry mem[32];	  // 0x1C
};