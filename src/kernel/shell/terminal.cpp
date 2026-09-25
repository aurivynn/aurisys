// the terminal

#include "shell/terminal.h"

#include "apps/app.h"
#include "drivers/console.h"
#include "drivers/fb.h"
#include "drivers/kbd.h"
#include "drivers/serial.h"
#include "lib/print.h"

#include <stdarg.h>
#include <stdint.h>

namespace terminal {

namespace {

const int kLineMax = 127; // +1 for the NUL

inline void outb(uint16_t port, uint8_t val) { asm volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }

inline uint8_t inb(uint16_t port) {
	uint8_t v;
	asm volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

uint16_t g_last_count = 0;
uint32_t g_ticks = 0;

void pit_init() {
	outb(0x43, 0x34); // ch0, mode 2, lobyte/hibyte
	outb(0x40, 0xA9);
	outb(0x40, 0x04); // reload 1193
}

uint32_t uptime_ms() {
	outb(0x43, 0x00); // latch ch0
	uint16_t count = inb(0x40) | (inb(0x40) << 8);
	if (count == 0)
		count = 1193;
	int32_t delta = (int32_t)g_last_count - (int32_t)count;
	if (delta < 0)
		delta += 1193; // counted down past zero, wrapped
	g_last_count = count;
	g_ticks += (uint32_t)delta;
	return g_ticks / 1193; // each tick is ~838ns
}

bool g_phase = false; // blink phase we last painted
bool g_painted = false;
int g_px = 0, g_py = 0;

void cursor_hide() {
	if (!g_painted)
		return;
	fb::fill_rect(g_px * 8, g_py * 16, 8, 16, console::bg());
	g_painted = false;
}

void cursor_paint() {
	cursor_hide();
	if (!g_phase)
		return;
	fb::fill_rect(console::cx() * 8 + 1, console::cy() * 16 + 1, 6, 14, 0x00FF9C);
	g_painted = true;
	g_px = console::cx();
	g_py = console::cy();
}

void cursor_tick() {
	const bool on = (uptime_ms() % 1000) < 500; // 500ms on, 500ms off
	if (on == g_phase)
		return;
	g_phase = on;
	cursor_paint();
}

// io
// one char to serial and the screen
void emit_both(char c) {
	serial::putc(c);
	console::putchar(c);
}

void vprint(const char* fmt, va_list ap) { print::vprintf(&emit_both, fmt, ap); }

// wait for a key (keyboard first, then serial), blinking while idle
int read_char() {
	for (;;) {
		cursor_tick();
		int c = kbd::poll();
		if (c >= 0)
			return c;
		c = serial::recv();
		if (c >= 0)
			return c;
	}
}

// split a line into argv (in place). returns argc, 0 = empty line
int tokenize(char* line, const char** argv, int argv_max) {
	int argc = 0;
	char* p = line;
	for (;;) {
		while (*p == ' ')
			++p;
		if (!*p)
			break;
		if (argc >= argv_max)
			break;
		argv[argc++] = p;
		while (*p && *p != ' ')
			++p;
		if (*p)
			*p++ = 0;
	}
	return argc;
}

void run_line(char* line) {
	const char* argv[8];
	const int argc = tokenize(line, argv, 8);
	if (argc == 0)
		return;
	if (apps::run(argv[0], argc, argv) < 0)
		terminal::printf("unknown app: %s\n", argv[0]);
}

void prompt() {
	console::setcolor(0x00FF9C, 0x101020); // green
	terminal::printf("aurisys> ");
	console::setcolor(0xE8E8E8, 0x101020);
	cursor_paint();
}

} // namespace

void printf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprint(fmt, ap);
	va_end(ap);
}

void run() {
	pit_init();
	char line[kLineMax + 1];
	for (;;) {
		prompt();
		int n = 0;
		for (;;) {
			const int c = read_char();
			cursor_hide(); // the block mustnot eat what we draw next
			if (c == '\n' || c == '\r') {
				emit_both('\n');
				break;
			}
			if (c == 0x03) { // ctrl+c: cancel the line
				emit_both('^');
				emit_both('C');
				emit_both('\n');
				break;
			}
			if (c == 0x7F) { // ctrl+backspace: kill the whole line
				while (n > 0) {
					emit_both('\b');
					emit_both(' ');
					emit_both('\b');
					--n;
				}
				cursor_paint();
				continue;
			}
			if (c == '\b' && n > 0) {
				--n;
				emit_both('\b');
				emit_both(' ');
				emit_both('\b');
				cursor_paint();
				continue;
			}
			if (c >= ' ' && c <= '~' && n < kLineMax) {
				line[n++] = (char)c;
				emit_both((char)c);
				cursor_paint();
			}
		}
		line[n] = 0;
		run_line(line);
	}
}

} // namespace terminal