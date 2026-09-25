#include "drivers/fb.h"

#include "fonts/font.h"
#include "lib/mem.h"

#include <stdint.h>

namespace fb {

uint32_t g_addr = 0;
uint32_t g_pitch = 0;
uint32_t g_width = 0;
uint32_t g_height = 0;

uint32_t addr() { return g_addr; }
uint32_t pitch() { return g_pitch; }
uint32_t width() { return g_width; }
uint32_t height() { return g_height; }

void init(uint32_t addr, uint32_t pitch, uint16_t width, uint16_t height) {
	g_addr = addr;
	g_pitch = pitch;
	g_width = width;
	g_height = height;
}

void clear(uint32_t color) {
	for (uint32_t y = 0; y < g_height; ++y) {
		uint32_t* row = (uint32_t*)(g_addr + y * g_pitch);
		for (uint32_t x = 0; x < g_width; ++x)
			row[x] = color;
	}
}

void fill_rect(int x, int y, int w, int h, uint32_t color) {
	for (int yy = y; yy < y + h; ++yy) {
		if (yy < 0 || (uint32_t)yy >= g_height)
			continue;
		uint32_t* row = (uint32_t*)(g_addr + (uint32_t)yy * g_pitch);
		for (int xx = x; xx < x + w; ++xx) {
			if (xx < 0 || (uint32_t)xx >= g_width)
				continue;
			row[xx] = color;
		}
	}
}

void drawchar(int x, int y, char c, uint32_t fg, uint32_t bg) {
	const uint8_t* glyph = font8x16_vga[(uint8_t)c];
	for (int yy = 0; yy < 16; ++yy) {
		const uint8_t bits = glyph[yy];
		uint32_t* row = (uint32_t*)(g_addr + (uint32_t)(y + yy) * g_pitch);
		for (int xx = 0; xx < 8; ++xx) {
			const uint32_t color = (bits & (0x80u >> xx)) ? fg : bg;
			const int px = x + xx;
			if (px < 0 || (uint32_t)px >= g_width)
				continue;
			row[px] = color;
		}
	}
}

} // namespace fb