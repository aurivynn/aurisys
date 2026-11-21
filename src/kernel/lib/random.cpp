#include "lib/random.h"

#include "arch/io.h"
#include "lib/time.h"

#include <stdint.h>

namespace random {

static uint32_t g_state;
static bool g_seeded;
static int g_rdrand = -1; // -1 unknown, 0 absent, 1 present

static uint32_t mix32(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

static uint32_t pit_word() {
	const uint8_t lo = io::inb(0x40);
	const uint8_t hi = io::inb(0x40);
	return ((uint32_t)hi << 8) | lo;
}

static bool have_rdrand() {
	if (g_rdrand >= 0)
		return g_rdrand != 0;
	uint32_t a = 0, b = 0, c = 0, d = 0;
	asm volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
	g_rdrand = 0;
	if (a >= 1) {
		a = 1;
		asm volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
		if (c & (1u << 30))
			g_rdrand = 1;
	}
	return g_rdrand != 0;
}

static bool hw_word(uint32_t* out) {
	if (!have_rdrand())
		return false;
	uint32_t v = 0;
	uint8_t ok = 0;

	asm volatile(".byte 0x0f, 0xc7, 0xf0\n\tsetc %1" : "=a"(v), "=m"(ok) : : "cc");
	if (!ok)
		return false;
	*out = v;
	return true;
}

static uint32_t seed() {
	uint32_t s = (uint32_t)time::ms() ^ (uint32_t)time::unix();
	for (int i = 0; i < 32; ++i) {
		for (volatile int spin = 0; spin < 37; ++spin) {
		}
		s = mix32(s ^ pit_word() ^ (uint32_t)i);
	}
	return s ? s : 0x1D2C3B4Au;
}

static uint32_t next() {
	if (!g_seeded) {
		g_state = seed();
		g_seeded = true;
	}
	g_state ^= g_state << 13;
	g_state ^= g_state >> 17;
	g_state ^= g_state << 5;
	return mix32(g_state);
}

void fill(void* buf, uint32_t len) {
	uint8_t* out = (uint8_t*)buf;
	uint32_t got = 0;
	while (got < len) {
		uint32_t v = 0;
		if (!hw_word(&v))
			v = next();
		for (int i = 0; i < 4 && got < len; ++i, ++got)
			out[got] = (uint8_t)(v >> (i * 8));
	}
}

} // namespace random
