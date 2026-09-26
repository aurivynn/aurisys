#include "drivers/pic.h"

#include <stdint.h>

namespace {
inline void outb(uint16_t port, uint8_t val) { asm volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }

inline uint8_t inb(uint16_t port) {
	uint8_t val;
	asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}
} // namespace

namespace pic {

// irqs only fire when we unmask them
void remap() {
	outb(0x20, 0x11); // icw1: init + edge
	outb(0xA0, 0x11);
	outb(0x21, 0x20); // icw2: master vectors 0x20+
	outb(0xA1, 0x28); // icw2: slave vectors 0x28+
	outb(0x21, 0x04); // icw3: slave on cascade line 2
	outb(0xA1, 0x02);
	outb(0x21, 0x01); // icw4: 8086 mode
	outb(0xA1, 0x01);
	outb(0x21, 0xFF); // mask all
	outb(0xA1, 0xFF);
}

//  Shut uyop
void mask_all() {
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);
}

void unmask(int irq) {
	if (irq < 0 || irq > 15)
		return;
	if (irq < 8) {
		outb(0x21, inb(0x21) & ~(uint8_t)(1u << irq));
	} else {
		outb(0xA1, inb(0xA1) & ~(uint8_t)(1u << (irq - 8)));
		outb(0x21, inb(0x21) & ~(uint8_t)(1u << 2)); // cascade line
	}
}

void eoi(int irq) {
	if (irq < 0 || irq > 15)
		return;
	if (irq >= 8)
		outb(0xA0, 0x20); // nonspecific eoi, slave
	outb(0x20, 0x20);	  // nonspecific eoi, master
}

} // namespace pic