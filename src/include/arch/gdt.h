#pragma once

#include <stdint.h>

// ring 3 stuff
// gdt entries for apps + tss the cpu switches to when apps trap
// needs a ring 0 stack to land on
constexpr uint16_t kSelKernCode = 0x08;
constexpr uint16_t kSelKernData = 0x10;
constexpr uint16_t kSelUserCode = 0x1B;
constexpr uint16_t kSelUserData = 0x23;
constexpr uint16_t kSelTss = 0x28;

void gdt_init();
// point the cpus ring 0 stack at a processes own kernel stack
void gdt_set_kernel_stack(uint32_t esp0);