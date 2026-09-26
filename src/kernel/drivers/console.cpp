#include "drivers/console.h"

#include "drivers/fb.h"
#include "lib/print.h"

#include <stdint.h>

namespace console {

namespace {

uint32_t g_fg = 0xCDD6F4;
uint32_t g_bg = 0x1E1E2E;
int g_cx = 0; // cursor in cells
int g_cy = 0;

uint32_t cols() { return fb::width() / 8; }
uint32_t rows() { return fb::height() / 16; }

void emit(char c) { putchar(c); }

} // namespace

void init() {
	g_fg = 0xCDD6F4;
	g_bg = 0x1E1E2E;
	g_cx = 0;
	g_cy = 0;
}

void setcolor(uint32_t fg, uint32_t bg) {
	g_fg = fg;
	g_bg = bg;
}

void clear() {
	fb::clear(g_bg);
	g_cx = 0;
	g_cy = 0;
}

void putchar(char c) {
	switch (c) {
	case '\n':
		g_cx = 0;
		++g_cy;
		break;
	case '\r':
		g_cx = 0;
		break;
	case '\t':
		++g_cx;
		while (g_cx % 4)
			++g_cx;
		break;
	case '\b': // erase the cell behind the cursor
		if (g_cx > 0)
			--g_cx;
		fb::drawchar(g_cx * 8, g_cy * 16, ' ', g_fg, g_bg);
		break;
	default:
		fb::drawchar(g_cx * 8, g_cy * 16, c, g_fg, g_bg);
		++g_cx;
		break;
	}
	if (g_cx >= (int)cols()) {
		g_cx = 0;
		++g_cy;
	}
	if (g_cy >= (int)rows()) {
		g_cx = 0;
		g_cy = 0; // wrap for now, no scroll
	}
}

void puts(const char* s) {
	while (*s)
		putchar(*s++);
}

void printf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print::vprintf(&emit, fmt, ap);
	va_end(ap);
}

int cx() { return g_cx; }

int cy() { return g_cy; }

uint32_t bg() { return g_bg; }

} // namespace console