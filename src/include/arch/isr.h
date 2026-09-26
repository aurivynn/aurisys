#pragma once

#include <stdint.h>

// pushed by isr.asm
struct Registers {
	uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
	uint32_t int_no, err_code, eip, cs, eflags;
};

typedef void (*irq_handler_t)(Registers*);

void irq_install(int irq, irq_handler_t fn); // irq 0..15

extern "C" void isr_common(Registers* r); // jumped to by the shared stub