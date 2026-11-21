#include "lib/klog.h"

#include "drivers/console.h"
#include "drivers/serial.h"
#include "lib/print.h"

#include <stdarg.h>

namespace klog {

static constexpr uint32_t kRingSize = 4096;
static char g_ring[kRingSize];
static uint32_t g_start = 0;
static uint32_t g_end = 0;

void putc(char c) {
	g_ring[g_end % kRingSize] = c;
	++g_end;
	if (g_end - g_start > kRingSize)
		g_start = g_end - kRingSize;

	serial::putc(c);
	console::putchar(c);
}

void printf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print::vprintf(&putc, fmt, ap);
	va_end(ap);
}

uint32_t oldest() { return g_start; }

int read(uint32_t off, void* buf, uint32_t len) {
	if (!buf || len == 0)
		return 0;
	if (off < g_start)
		off = g_start;
	if (off >= g_end)
		return 0;

	uint32_t n = g_end - off;
	if (n > len)
		n = len;
	uint8_t* out = (uint8_t*)buf;
	for (uint32_t i = 0; i < n; ++i)
		out[i] = (uint8_t)g_ring[(off + i) % kRingSize];
	return (int)n;
}

} // namespace klog
