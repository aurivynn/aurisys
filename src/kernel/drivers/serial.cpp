#include "drivers/serial.h"

#include "arch/io.h"

#include <stdint.h>

#define COM1 0x3F8u

namespace serial {

void init() {
	io::outb(COM1 + 1, 0x00); // no irqs
	io::outb(COM1 + 3, 0x80); // dlab on
	io::outb(COM1 + 0, 0x01); // 115200
	io::outb(COM1 + 1, 0x00);
	io::outb(COM1 + 3, 0x03); // 8n1
	io::outb(COM1 + 2, 0xC7); // fifo on
}

void putc(char c) {
	while ((io::inb(COM1 + 5) & 0x20u) == 0) {
		// wait for the tx buffer
	}
	io::outb(COM1, (uint8_t)c);
}

void puts(const char* s) {
	while (*s)
		putc(*s++);
}

void write(const char* s, int n) {
	for (int i = 0; i < n; ++i)
		putc(s[i]);
}

int recv() {
	if ((io::inb(COM1 + 5) & 1) == 0)
		return -1; // rx fifo empty
	return io::inb(COM1);
}

bool pending() { return (io::inb(COM1 + 5) & 1) != 0; }

} // namespace serial
