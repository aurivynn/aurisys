#pragma once

#include <stdint.h>

// text console on the framebuffer. 1 char = 8x16 px.
namespace console {

void init();
void setcolor(uint32_t fg, uint32_t bg);
void clear();
void putchar(char c);
void puts(const char* s);
void printf(const char* fmt, ...);

} // namespace console