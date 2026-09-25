#include "drivers/kbd.h"

#include <stdint.h>

namespace {
inline uint8_t inb(uint16_t port) {
	uint8_t val;
	asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

// scancode set 1 -> ascii. 0 = key we ignore.
const char kScan[96] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// 0x0E backspace, 0x1C enter, 0x1D ctrl, 0x2A/0x36 shift
bool g_shift = false;
bool g_ctrl = false;
} // namespace

namespace kbd {

int poll() {
	if ((inb(0x64) & 1) == 0)
		return -1; // no data
	uint8_t sc = inb(0x60);

	if (sc == 0xE0)
		return -2; // extended prefix, swallow it
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
	if (sc == 0x2A || sc == 0x36)
		return -2; // shift already handled

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