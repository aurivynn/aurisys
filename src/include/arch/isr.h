#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
	kFrEdi = 0,
	kFrEsi,
	kFrEbp,
	kFrEsp, // the esp pushad saw, which in ring 3 is the user esp
	kFrEbx,
	kFrEdx,
	kFrEcx,
	kFrEax,
	kFrIntNo,
	kFrErr,
	kFrEip,
	kFrCs,
	kFrEflags,
	kFrUserEsp, // pushed by the cpu itself, ring 3 only
	kFrUserSs,	// pushed by the cpu itself, ring 3 only
	kFrWords,
};

struct Registers {
	uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
	uint32_t int_no, err_code, eip, cs, eflags;
	uint32_t user_esp, user_ss;
};

static_assert(offsetof(Registers, edi) / 4 == kFrEdi, "edi is frame word 0");
static_assert(offsetof(Registers, esp_dummy) / 4 == kFrEsp, "esp_dummy is frame word 3");
static_assert(offsetof(Registers, int_no) / 4 == kFrIntNo, "int_no is frame word 8");
static_assert(offsetof(Registers, eip) / 4 == kFrEip, "eip is frame word 10");
static_assert(offsetof(Registers, cs) / 4 == kFrCs, "cs is frame word 11");
static_assert(offsetof(Registers, eflags) / 4 == kFrEflags, "eflags is frame word 12");
static_assert(offsetof(Registers, user_esp) / 4 == kFrUserEsp, "user_esp is frame word 13");
static_assert(offsetof(Registers, user_ss) / 4 == kFrUserSs, "user_ss is frame word 14");
static_assert(sizeof(Registers) / 4 == kFrWords, "the save area must be the whole frame");

typedef void (*irq_handler_t)(Registers*);

void irq_install(int irq, irq_handler_t fn); // irq 0..15

extern "C" void isr_common(Registers* r); // jumped to by the shared stub