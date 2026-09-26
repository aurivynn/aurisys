#include "drivers/kbd.h"

#include "arch/isr.h"
#include "drivers/pic.h"

#include <stdint.h>

namespace {
inline uint8_t inb(uint16_t port) {
	uint8_t val;
	asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

// scancode set 1 -> ascii. 0 = key we ignore.
const char kScan[96] = {
	0,	  0,   '1', '2',  '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,	 0,	  'q', 'w', 'e', 'r',
	't',  'y', 'u', 'i',  'o', 'p', '[', ']', 0,   0,	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
	'\'', '`', 0,	'\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,	 '*', 0,   ' ', 0,	 0,
	0,	  0,   0,	0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,
	0,	  0,   0,	0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,	  0,   0,	0,	 0,
};

const int kBuf = 128;
volatile uint8_t g_buf[kBuf];
volatile int g_head = 0;
volatile int g_tail = 0;

void irq_handler(Registers*) {
	const uint8_t sc = inb(0x60);
	const int next = (g_head + 1) % kBuf;
	if (next != g_tail) { // not full
		g_buf[g_head] = sc;
		g_head = next;
	}
}

// 0x0E backspace, 0x1C enter, 0x1D ctrl, 0x2A/0x36 shift
bool g_shift = false;
bool g_ctrl = false;
bool g_ext = false;
} // namespace

int key_of(uint8_t sc) {
	switch (sc) {
	case 0x47:
		return kbd::KEY_HOME;
	case 0x48:
		return kbd::KEY_UP;
	case 0x49:
		return kbd::KEY_PGUP;
	case 0x4B:
		return kbd::KEY_LEFT;
	case 0x4D:
		return kbd::KEY_RIGHT;
	case 0x4F:
		return kbd::KEY_END;
	case 0x50:
		return kbd::KEY_DOWN;
	case 0x51:
		return kbd::KEY_PGDN;
	case 0x52:
		return kbd::KEY_INS;
	case 0x53:
		return kbd::KEY_DEL;
	}
	return -2;
}

namespace kbd {

void init() {
	irq_install(1, irq_handler);
	pic::unmask(1);
}

int poll() {
	if (g_head == g_tail)
		return -1; // nothing queued
	const uint8_t sc = g_buf[g_tail];
	g_tail = (g_tail + 1) % kBuf;

	if (sc == 0xE0) {
		g_ext = true; // the next scancode is an extended one
		return -2;
	}
	if (g_ext) {
		g_ext = false; // consumed by this byte
		if (sc & 0x80)
			return -2; // extended break, swallow it
		if (sc >= 0x47 && sc <= 0x53)
			return key_of(sc);
		return -2; // other extended key (ctrl_r, windows, etcc), ignore
	}

	if (sc == 0x2A || sc == 0x36)
		g_shift = true;
	else if (sc == 0xAA || sc == 0xB6)
		g_shift = false; // shift break
	else if (sc == 0x1D)
		g_ctrl = true;
	else if (sc == 0x9D)
		g_ctrl = false; // ctrl break
	else if (sc & 0x80)
		return -2; // some other key released, ignore

	if (g_ctrl && sc == 0x0E)
		return 0x7F; // ctrl+backspace = kill the line
	if (g_ctrl && sc == 0x2E)
		return 0x03; // ctrl+c = cancel the line

	if (sc == 0x0E)
		return '\b';
	if (sc == 0x1C)
		return '\n';
	if (sc == 0x0F)
		return '\t';
	if (sc == 0x2A || sc == 0x36)
		return -2; // shift already handled

	if (sc >= 0x47 && sc <= 0x53)
		return key_of(sc);

	if (sc >= 96)
		return -2; // past the table, ignore

	char c = kScan[sc];
	if (!c)
		return -2;
	if (g_shift && c >= 'a' && c <= 'z')
		c -= 32; // uppercase
	return c;
}

} // namespace kbd