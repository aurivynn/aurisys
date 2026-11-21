#include "drivers/timer.h"

#include "arch/io.h"
#include "arch/isr.h"
#include "drivers/pic.h"
#include "drivers/rtc.h"
#include "lib/time.h"

#include <stdint.h>

static const uint16_t kReload = 1193;

static volatile uint64_t g_ticks = 0;

static void tick(Registers*) { ++g_ticks; }

constexpr uint32_t kPitHz = 1193182u;
constexpr uint32_t kTickThousandths = (uint32_t)(((uint64_t)kReload * 1000000u + kPitHz / 2u) / kPitHz);

namespace time {

static uint32_t g_unix_base = 0;
static uint32_t g_unix_anchor = 0;

// ticks to milliseconds
uint32_t ms() {
	const uint32_t lo = (uint32_t)g_ticks;
	const uint32_t hi = (uint32_t)(g_ticks >> 32);
	const uint32_t whole = kTickThousandths / 1000u;
	const uint32_t part = kTickThousandths % 1000u;
	return lo * whole + (lo * part) / 1000u + (hi * part) / 1000u;
}

bool init() {
	uint32_t base = 0;
	const bool ok = rtc::read(&base);
	g_unix_base = ok ? base : 0;
	g_unix_anchor = ms();
	return ok;
}

uint32_t unix() { return g_unix_base + (ms() - g_unix_anchor) / 1000u; }

} // namespace time

uint32_t ticks_for_ms(uint32_t ms) {
	const uint32_t whole = ms / kTickThousandths;
	const uint32_t part = ms % kTickThousandths;
	return whole * 1000u + (part * 1000u + kTickThousandths / 2u) / kTickThousandths;
}

void timer_init() {
	io::outb(0x43, 0x34); // ch0, mode 2 (rate gen), lobyte/hibyte
	io::outb(0x40, kReload & 0xFF);
	io::outb(0x40, kReload >> 8);
	irq_install(0, tick);
	pic::unmask(0);
}
