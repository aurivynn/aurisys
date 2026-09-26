#include "arch/idt.h"

#include <stdint.h>

extern "C" {
extern const uint32_t isr_stubs[32];
extern const uint32_t irq_stubs[16];
extern void syscall_stub();
}

extern "C" {
idt_entry g_idt[256];
idt_desc g_idt_desc;
}

namespace {

void set_gate(int i, uint32_t off) {
	g_idt[i].off_lo = (uint16_t)(off & 0xFFFF);
	g_idt[i].sel = 0x08; // kernel code segment
	g_idt[i].zero = 0;
	g_idt[i].flags = 0x8E; // present, dpl0, interrupt gate
	g_idt[i].off_hi = (uint16_t)(off >> 16);
}

} // namespace

void idt_init() {
	for (int i = 0; i < 256; ++i) {
		g_idt[i].off_lo = 0;
		g_idt[i].sel = 0;
		g_idt[i].zero = 0;
		g_idt[i].flags = 0; // not present
		g_idt[i].off_hi = 0;
	}
	for (int i = 0; i < 32; ++i)
		set_gate(i, isr_stubs[i]);
	for (int i = 0; i < 16; ++i)
		set_gate(32 + i, irq_stubs[i]);
	// the app gate. dpl 0 for now since apps run on ring 0 widens it to dpl 3
	set_gate(0x80, (uint32_t)(uintptr_t)&syscall_stub);

	g_idt_desc.limit = (uint16_t)(sizeof(g_idt) - 1);
	g_idt_desc.base = (uint32_t)g_idt;
	asm volatile("lidt %0" : : "m"(g_idt_desc) : "memory");
}