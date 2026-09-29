#include "drivers/timer.h"

#include "arch/isr.h"
#include "drivers/pic.h"
#include "lib/time.h"

#include <stdint.h>

namespace {
inline void outb(uint16_t port, uint8_t val) { asm volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }

const uint16_t kReload = 1193;

volatile uint64_t g_ms = 0;

void tick(Registers*) { ++g_ms; }
} // namespace

namespace time {

uint32_t ms() { return (uint32_t)g_ms; }

} // namespace time

constexpr uint32_t kPitHz = 1193182u;

uint32_t ticks_for_ms(uint32_t ms) {
	const uint32_t num = ms * kPitHz + kReload * 500u;
	return num / (kReload * 1000u);
}

void timer_init() {
	outb(0x43, 0x34); // ch0, mode 2 (rate gen), lobyte/hibyte
	outb(0x40, kReload & 0xFF);
	outb(0x40, kReload >> 8);
	irq_install(0, tick);
	pic::unmask(0);
}