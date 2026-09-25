#include "drivers/serial.h"

#include <stdint.h>

#define COM1 0x3F8u

namespace {
inline void outb(uint16_t port, uint8_t val) { asm volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }

inline uint8_t inb(uint16_t port) {
	uint8_t val;
	asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}
} // namespace

namespace serial {

void init() {
	outb(COM1 + 1, 0x00); // no irqs
	outb(COM1 + 3, 0x80); // dlab on
	outb(COM1 + 0, 0x01); // 115200
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x03); // 8n1
	outb(COM1 + 2, 0xC7); // fifo on
}

void putc(char c) {
	while ((inb(COM1 + 5) & 0x20u) == 0) {
		// wait for the tx buffer
	}
	outb(COM1, (uint8_t)c);
}

void puts(const char* s) {
	while (*s)
		putc(*s++);
}

} // namespace serial