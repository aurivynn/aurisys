#pragma once

#include <stdint.h>

// the vga 8x16 font blob, stuck in the image by fonts/font.asm
extern "C" const uint8_t font8x16_vga[256][16];