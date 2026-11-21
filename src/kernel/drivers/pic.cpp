#include "drivers/pic.h"

#include "arch/io.h"

#include <stdint.h>

namespace pic {

// irqs only fire when we unmask them
void remap() {
	io::outb(0x20, 0x11); // icw1: init + edge
	io::outb(0xA0, 0x11);
	io::outb(0x21, 0x20); // icw2: master vectors 0x20+
	io::outb(0xA1, 0x28); // icw2: slave vectors 0x28+
	io::outb(0x21, 0x04); // icw3: slave on cascade line 2
	io::outb(0xA1, 0x02);
	io::outb(0x21, 0x01); // icw4: 8086 mode
	io::outb(0xA1, 0x01);
	io::outb(0x21, 0xFF); // mask all
	io::outb(0xA1, 0xFF);
}

//  Shut uyop
void mask_all() {
	io::outb(0x21, 0xFF);
	io::outb(0xA1, 0xFF);
}

void unmask(int irq) {
	if (irq < 0 || irq > 15)
		return;
	if (irq < 8) {
		io::outb(0x21, io::inb(0x21) & ~(uint8_t)(1u << irq));
	} else {
		io::outb(0xA1, io::inb(0xA1) & ~(uint8_t)(1u << (irq - 8)));
		io::outb(0x21, io::inb(0x21) & ~(uint8_t)(1u << 2)); // cascade line
	}
}

void eoi(int irq) {
	if (irq < 0 || irq > 15)
		return;
	if (irq >= 8)
		io::outb(0xA0, 0x20); // nonspecific eoi, slave
	io::outb(0x20, 0x20);	  // nonspecific eoi, master
}

} // namespace pic
