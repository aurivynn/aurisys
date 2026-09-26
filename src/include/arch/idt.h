#pragma once

#include <stdint.h>

// 256-slot idt, 32bit interrupt gates, no tss/ist.
struct idt_entry {
	uint16_t off_lo; // handler bits 0..15
	uint16_t sel;	 // kernel code segment (0x08)
	uint8_t zero;
	uint8_t flags; // 0x8E = present, dpl0, 32 bit gate
	uint16_t off_hi;
} __attribute__((packed));

struct idt_desc {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed));

extern "C" {
void idt_init();
extern idt_entry g_idt[256];
extern idt_desc g_idt_desc; // this gets loaded early
}