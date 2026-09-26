#pragma once
// https://wiki.osdev.org/8259_PIC

// 8259 pic: master remapped to 0x20, slave to 0x28.
namespace pic {

void remap();		  // re-wire + mask everything. call with interrupts off
void mask_all();	  // no reinit for the panic path
void unmask(int irq); // irq 0..15 as the cpu sees them
void eoi(int irq);	  // ack an irq after the handler ran

} // namespace pic