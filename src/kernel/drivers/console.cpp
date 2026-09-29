#include "drivers/console.h"

#include "drivers/fb.h"
#include "lib/print.h"

#include <stdint.h>

namespace console {

namespace {

constexpr uint32_t kDefaultFg = 0xCDD6F4;
constexpr uint32_t kDefaultBg = 0x1E1E2E;

const uint32_t kAnsi[8] = {0x1E1E2E, 0xF38BA8, 0xA6E3A1, 0xF9E2AF, 0x89B4FA, 0xCBA6F7, 0x94E2D5, 0xCDD6F4};
const uint32_t kAnsiBright[8] = {0x45475A, 0xF38BA8, 0xA6E3A1, 0xF9E2AF, 0x89B4FA, 0xCBA6F7, 0x94E2D5, 0xFFFFFF};

uint32_t ansi_fg(int n, bool bright = false) {
	if (n < 0 || n > 7)
		return kDefaultFg;
	return bright ? kAnsiBright[n] : kAnsi[n];
}

uint32_t lighten(uint32_t c) {
	const uint32_t r = (c >> 16) & 0xFFu;
	const uint32_t g = (c >> 8) & 0xFFu;
	const uint32_t b = c & 0xFFu;
	const uint32_t nr = (r + 0xFFu) / 2u;
	const uint32_t ng = (g + 0xFFu) / 2u;
	const uint32_t nb = (b + 0xFFu) / 2u;
	return (nr << 16) | (ng << 8) | nb;
}

uint32_t g_fg = kDefaultFg;
uint32_t g_bg = kDefaultBg;
int g_cx = 0; // cursor in cells
int g_cy = 0;

int g_cur_x = -1; // the cell currently drawn as a cursor block
int g_cur_y = -1;
bool g_cursor_on = true;

constexpr int kMaxCols = 256;
constexpr int kMaxRows = 64;
char g_ch[kMaxRows][kMaxCols];
uint32_t g_fgc[kMaxRows][kMaxCols];

int cols() {
	const int n = (int)(fb::width() / 8);
	return n < kMaxCols ? n : kMaxCols;
}

int rows() {
	const int n = (int)(fb::height() / 16);
	return n < kMaxRows ? n : kMaxRows;
}

void emit(char c) { putchar(c); }

int clampx(int x) {
	if (x < 0)
		x = 0;
	if (x >= cols())
		x = cols() - 1;
	return x;
}

int clampy(int y) {
	if (y < 0)
		y = 0;
	if (y >= rows())
		y = rows() - 1;
	return y;
}

void paint(int x, int y) {
	fb::drawchar(x * 8, y * 16, g_ch[y][x], g_fgc[y][x], g_bg);
	if (g_cursor_on && x == g_cx && y == g_cy)
		fb::fill_rect(x * 8, y * 16 + 13, 8, 3, g_fgc[y][x]);
}

void blank_row(int y) {
	for (int x = 0; x < cols(); ++x) {
		g_ch[y][x] = ' ';
		g_fgc[y][x] = g_fg;
	}
}

void blank_all() {
	for (int y = 0; y < rows(); ++y)
		blank_row(y);
}

// repaint the two cells the cursor is moving between and nothing else
void repaint_cursor() {
	if (g_cur_x >= 0 && (g_cur_x != g_cx || g_cur_y != g_cy))
		paint(g_cur_x, g_cur_y);
	if (g_cursor_on) {
		paint(g_cx, g_cy);
		g_cur_x = g_cx;
		g_cur_y = g_cy;
	} else {
		g_cur_x = -1;
	}
}

void repaint_all() {
	for (int y = 0; y < rows(); ++y)
		for (int x = 0; x < cols(); ++x)
			paint(x, y);
}

void move_to(int x, int y) {
	g_cx = clampx(x);
	g_cy = clampy(y);
	repaint_cursor();
}

void newline() {
	g_cx = 0;
	++g_cy;
	if (g_cy >= rows()) {
		if (g_cur_x >= 0) {
			paint(g_cur_x, g_cur_y);
			g_cur_x = -1;
		}

		for (int y = 0; y + 1 < rows(); ++y)
			for (int x = 0; x < cols(); ++x) {
				g_ch[y][x] = g_ch[y + 1][x];
				g_fgc[y][x] = g_fgc[y + 1][x];
			}
		fb::scroll_up(16, g_bg);
		g_cy = rows() - 1;
		blank_row(g_cy); // the row that scrolled in was never written
	}
	repaint_cursor();
}

void erase_cell(int x, int y) {
	g_ch[y][x] = ' ';
	g_fgc[y][x] = g_fg;
	paint(x, y);
}

void erase_line(int how) {
	if (how == 1) {
		for (int x = 0; x <= g_cx; ++x)
			erase_cell(x, g_cy);
	} else if (how == 2) {
		for (int x = 0; x < cols(); ++x)
			erase_cell(x, g_cy);
	} else {
		for (int x = g_cx; x < cols(); ++x)
			erase_cell(x, g_cy);
	}
	repaint_cursor();
}

void erase_display(int how) {
	if (how == 2 || how == 3) {
		blank_all();
		fb::clear(g_bg);
		g_cx = 0;
		g_cy = 0;
		repaint_cursor();
		return;
	}
	if (how == 1) {
		for (int y = 0; y < g_cy; ++y)
			blank_row(y);
		blank_row(g_cy);
		for (int x = 0; x <= g_cx; ++x)
			paint(x, g_cy);
		for (int y = 0; y < g_cy; ++y)
			for (int x = 0; x < cols(); ++x)
				paint(x, y);
	} else {
		for (int x = g_cx; x < cols(); ++x)
			paint(x, g_cy);
		for (int y = g_cy + 1; y < rows(); ++y) {
			blank_row(y);
			for (int x = 0; x < cols(); ++x)
				paint(x, y);
		}
	}
	repaint_cursor();
}

// escape sequence handling
enum { kPlain, kAfterEsc, kInCsi, kInOsc };

int g_state = kPlain;

constexpr int kMaxParam = 8;
int g_param[kMaxParam];
int g_nparam;
int g_pend;		 // a number being accumulated
bool g_pend_set; // whether a number has been accumulated at all
bool g_priv;	 // a private marker such as ? was seen

void csi_start() {
	g_nparam = 0;
	g_pend = 0;
	g_pend_set = false;
	g_priv = false;
}

void csi_push() {
	if (g_nparam < kMaxParam)
		g_param[g_nparam] = g_pend;
	++g_nparam;

	g_pend = 0;
	g_pend_set = false;
}

void sgr() {
	if (g_nparam == 0) {
		g_fg = kDefaultFg;
		g_bg = kDefaultBg;
	}

	for (int i = 0; i < g_nparam; ++i) {
		const int p = i < kMaxParam ? g_param[i] : 0;
		if (p == 0) {
			g_fg = kDefaultFg;
			g_bg = kDefaultBg;
		} else if (p == 1) { // bold: brighten rather than change hue
			g_fg = lighten(g_fg);
		} else if (p == 7) { // reverse
			const uint32_t t = g_fg;
			g_fg = g_bg;
			g_bg = t;
		} else if (p == 22) {
			g_fg = kDefaultFg;
		} else if (p == 27) {
			g_fg = kDefaultFg;
			g_bg = kDefaultBg;
		} else if (p >= 30 && p <= 37) {
			g_fg = ansi_fg(p - 30);
		} else if (p >= 90 && p <= 97) {
			g_fg = ansi_fg(p - 90, true);
		} else if (p == 39) {
			g_fg = kDefaultFg;
		} else if (p >= 40 && p <= 47) {
			g_bg = ansi_fg(p - 40);
		} else if (p >= 100 && p <= 107) {
			g_bg = ansi_fg(p - 100, true);
		} else if (p == 49) {
			g_bg = kDefaultBg;
		} else if ((p == 38 || p == 48) && i + 1 < g_nparam) {
			if (g_param[i + 1] == 2 && i + 4 < g_nparam) {
				const uint32_t c =
					((uint32_t)g_param[i + 2] << 16) | ((uint32_t)g_param[i + 3] << 8) | (uint32_t)g_param[i + 4];
				if (p == 38)
					g_fg = c;
				else
					g_bg = c;
				i += 4;
			} else if (g_param[i + 1] == 5 && i + 2 < g_nparam) {
				const uint32_t c = ansi_fg(g_param[i + 2] & 7, g_param[i + 2] > 7);
				if (p == 38)
					g_fg = c;
				else
					g_bg = c;
				i += 2;
			} else {
				++i;
			}
		}
	}
}

void csi_dispatch(char final) {
	const int a = g_param[0] ? g_param[0] : 1;
	const int b = g_param[1] ? g_param[1] : 1;
	switch (final) {
	case 'm':
		sgr();
		return;
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
			move_to(cols() - 1, g_cy); // end
		else if (a == 5)
			move_to(g_cx, 0); // page up
		else if (a == 6)
			move_to(g_cx, rows() - 1); // page down
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
			g_pend_set = true;
		} else if (c == ';') {
			csi_push();
		} else if (c == '?') {
			g_priv = true;
		} else if (c >= 0x40 && c <= 0x7E) {
			// pushed whether or not a number arrived & not only when one did
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
			g_state = kPlain; // and so does ST
		break;
	default:
		break;
	}
}

} // namespace

void init() {
	g_fg = kDefaultFg;
	g_bg = kDefaultBg;
	g_cx = 0;
	g_cy = 0;
	g_cur_x = -1;
	g_cur_y = -1;
	g_cursor_on = true;
	g_state = kPlain;
	blank_all();
}

void setcolor(uint32_t fg, uint32_t bg) {
	g_fg = fg;
	g_bg = bg;
	repaint_all();
}

void cursor_visible(bool on) {
	if (on == g_cursor_on)
		return;
	g_cursor_on = on;
	repaint_cursor();
}

bool cursor_is_visible() { return g_cursor_on; }

void clear() {
	blank_all();
	fb::clear(g_bg);
	g_cx = 0;
	g_cy = 0;
	repaint_cursor();
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
		return;
	case '\r':
		move_to(0, g_cy);
		return;
	case '\t':
		move_to((g_cx | 3) + 1, g_cy);
		break;
	case '\b': // erase the cell behind the cursor
		if (g_cx > 0)
			--g_cx;
		erase_cell(g_cx, g_cy);
		break;
	default:
		break;
	}

	g_ch[g_cy][g_cx] = c;
	g_fgc[g_cy][g_cx] = g_fg;
	paint(g_cx, g_cy);

	++g_cx;
	if (g_cx >= cols()) {
		g_cx = 0;
		++g_cy;
		if (g_cy >= rows()) {
			newline();
			return;
		}
	}
	repaint_cursor();
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
