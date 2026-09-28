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

int g_cur_x = -1; // where the cursor block is drawn
int g_cur_y = -1;
bool g_cursor_on = true;

uint32_t cols() { return fb::width() / 8; }
uint32_t rows() { return fb::height() / 16; }

void emit(char c) { putchar(c); }

int clampx(int x) {
	if (x < 0)
		x = 0;
	if (x >= (int)cols())
		x = (int)cols() - 1;
	return x;
}

int clampy(int y) {
	if (y < 0)
		y = 0;
	if (y >= (int)rows())
		y = (int)rows() - 1;
	return y;
}

// put the cursor block where the text cursor is
void cursor_refresh() {
	if (g_cur_x >= 0) {
		fb::drawchar(g_cur_x * 8, g_cur_y * 16, ' ', g_fg, g_bg);
		g_cur_x = -1;
	}
	if (g_cursor_on) {
		fb::drawchar(g_cx * 8, g_cy * 16, ' ', g_bg, g_fg);
		g_cur_x = g_cx;
		g_cur_y = g_cy;
	}
}

void move_to(int x, int y) {
	g_cx = clampx(x);
	g_cy = clampy(y);
	cursor_refresh();
}

void newline() {
	g_cx = 0;
	++g_cy;
	if (g_cy >= (int)rows()) {
		if (g_cur_y == (int)rows() - 1)
			g_cur_y = -1;
		fb::scroll_up(16, g_bg);
		g_cy = (int)rows() - 1;
	}
}

void erase_cell(int x, int y) { fb::drawchar(x * 8, y * 16, ' ', g_fg, g_bg); }

void erase_line(int how) {
	if (how == 1) {
		for (int x = 0; x <= g_cx; ++x)
			erase_cell(x, g_cy);
	} else if (how == 2) {
		for (int x = 0; x < (int)cols(); ++x)
			erase_cell(x, g_cy);
	} else {
		for (int x = g_cx; x < (int)cols(); ++x)
			erase_cell(x, g_cy);
	}
	cursor_refresh();
}

void erase_display(int how) {
	if (how == 2 || how == 3) {
		fb::clear(g_bg);
		move_to(0, 0);
		return;
	}
	if (how == 1) {
		for (int y = 0; y < g_cy; ++y)
			for (int x = 0; x < (int)cols(); ++x)
				erase_cell(x, y);
		erase_line(1);
	} else {
		erase_line(0);
		for (int y = g_cy + 1; y < (int)rows(); ++y)
			for (int x = 0; x < (int)cols(); ++x)
				erase_cell(x, y);
	}
	cursor_refresh();
}

// escape sequence handling
enum { kPlain, kAfterEsc, kInCsi, kInOsc };

int g_state = kPlain;
int g_param[4];
int g_nparam;
int g_pend;	 // a number being accumulated
bool g_priv; // a private marker such as ? was seen

void csi_start() {
	g_nparam = 0;
	g_pend = 0;
	g_priv = false;
}

void csi_push() {
	if (g_nparam < (int)(sizeof g_param / sizeof g_param[0]))
		g_param[g_nparam] = g_pend;
	++g_nparam;
	g_pend = 0;
}

void csi_dispatch(char final) {
	const int a = g_param[0] ? g_param[0] : 1;
	const int b = g_param[1] ? g_param[1] : 1;
	switch (final) {
	case 'A':
		move_to(g_cx, g_cy - a);
		break;
	case 'B':
		move_to(g_cx, g_cy + a);
		break;
	case 'C':
		move_to(g_cx + a, g_cy);
		break;
	case 'D':
		move_to(g_cx - a, g_cy);
		break;
	case 'G':
		move_to(a - 1, g_cy);
		break;
	case 'H':
	case 'f':
		move_to(b - 1, a - 1);
		break;
	case 'J':
		erase_display(a);
		break;
	case 'K':
		erase_line(a);
		break;
	case 'h':
		if (g_priv && a == 25)
			cursor_visible(true);
		break;
	case 'l':
		if (g_priv && a == 25)
			cursor_visible(false);
		break;
	case '~':
		if (a == 1)
			move_to(0, g_cy); // home
		else if (a == 4)
			move_to((int)cols() - 1, g_cy); // end
		else if (a == 5)
			move_to(g_cx, 0); // page up
		else if (a == 6)
			move_to(g_cx, (int)rows() - 1); // page down
		break;
	default:
		break;
	}
}

void escape(char c) {
	switch (g_state) {
	case kAfterEsc:
		if (c == '[') {
			g_state = kInCsi;
			csi_start();
		} else if (c == ']') {
			g_state = kInOsc;
		} else {
			g_state = kPlain;
		}
		break;
	case kInCsi:
		if (c >= '0' && c <= '9') {
			g_pend = g_pend * 10 + (c - '0');
		} else if (c == ';') {
			csi_push();
		} else if (c == '?') {
			g_priv = true;
		} else if (c >= 0x40 && c <= 0x7E) {
			if (g_pend)
				csi_push();
			csi_dispatch(c);
			g_state = kPlain;
		} else if (c == 0x1b) {
			g_state = kAfterEsc; // started again mid sequence
		}
		break;
	case kInOsc:
		if (c == 0x07)
			g_state = kPlain; // bell ends it
		else if (c == 0x1b)
			g_state = kAfterEsc; // and so does ST
		break;
	default:
		break;
	}
}

} // namespace

void init() {
	g_fg = 0xCDD6F4;
	g_bg = 0x1E1E2E;
	g_cx = 0;
	g_cy = 0;
	g_cur_x = -1;
	g_cur_y = -1;
	g_cursor_on = true;
	g_state = kPlain;
}

void setcolor(uint32_t fg, uint32_t bg) {
	g_fg = fg;
	g_bg = bg;
}

void cursor_visible(bool on) {
	if (on == g_cursor_on)
		return;
	g_cursor_on = on;
	cursor_refresh();
}

bool cursor_is_visible() { return g_cursor_on; }

void clear() {
	g_cur_x = -1;
	g_cur_y = -1;
	fb::clear(g_bg);
	g_cx = 0;
	g_cy = 0;
	cursor_refresh();
}

void gotoxy(int cell_x, int cell_y) { move_to(cell_x, cell_y); }

void putchar(char c) {
	if (g_state != kPlain) {
		escape(c);
		return;
	}
	switch (c) {
	case 0x1b: // the start of a sequence
		g_state = kAfterEsc;
		return;
	case '\n':
		newline();
		cursor_refresh();
		return;
	case '\r':
		g_cx = 0;
		cursor_refresh();
		return;
	case '\t':
		++g_cx;
		while (g_cx % 4)
			++g_cx;
		break;
	case '\b': // erase the cell behind the cursor
		if (g_cx > 0)
			--g_cx;
		erase_cell(g_cx, g_cy);
		break;
	default:
		break;
	}
	fb::drawchar(g_cx * 8, g_cy * 16, c, g_fg, g_bg);
	if (++g_cx >= (int)cols())
		newline();
	cursor_refresh();
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

uint32_t fg() { return g_fg; }

} // namespace console
