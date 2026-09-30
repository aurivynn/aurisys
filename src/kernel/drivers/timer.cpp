#include "drivers/timer.h"

#include "arch/isr.h"
#include "drivers/pic.h"
#include "lib/time.h"

#include <stdint.h>

namespace {
inline void outb(uint16_t port, uint8_t val) { asm volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }

const uint16_t kReload = 1193;

volatile uint64_t g_ticks = 0;

void tick(Registers*) { ++g_ticks; }
} // namespace

constexpr uint32_t kPitHz = 1193182u;
constexpr uint32_t kTickThousandths = (uint32_t)(((uint64_t)kReload * 1000000u + kPitHz / 2u) / kPitHz);

namespace time {

// ticks to milliseconds
uint32_t ms() {
	const uint32_t lo = (uint32_t)g_ticks;
	const uint32_t hi = (uint32_t)(g_ticks >> 32);
	const uint32_t whole = kTickThousandths / 1000u;
	const uint32_t part = kTickThousandths % 1000u;
	return lo * whole + (lo * part) / 1000u + (hi * part) / 1000u;
}

} // namespace time

uint32_t ticks_for_ms(uint32_t ms) {
	const uint32_t whole = ms / kTickThousandths;
	const uint32_t part = ms % kTickThousandths;
	return whole * 1000u + (part * 1000u + kTickThousandths / 2u) / kTickThousandths;
}

void timer_init() {
	outb(0x43, 0x34); // ch0, mode 2 (rate gen), lobyte/hibyte
	outb(0x40, kReload & 0xFF);
	outb(0x40, kReload >> 8);
	irq_install(0, tick);
	pic::unmask(0);
}
