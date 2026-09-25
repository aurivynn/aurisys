#pragma once

#include <stdint.h>

namespace fb {

void init(uint32_t addr, uint32_t pitch, uint16_t width, uint16_t height);
void clear(uint32_t color);
void fill_rect(int x, int y, int w, int h, uint32_t color);
void drawchar(int x, int y, char c, uint32_t fg, uint32_t bg);

uint32_t addr();
uint32_t pitch();
uint32_t width();
uint32_t height();

} // namespace fb