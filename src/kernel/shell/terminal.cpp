#include "shell/terminal.h"

#include "drivers/console.h"
#include "drivers/serial.h"
#include "lib/print.h"
#include "vfs.h"

#include <stdint.h>

// The kernels console, and nothing else.
namespace terminal {

namespace {
constexpr int kPutMax = 1024;
char g_put[kPutMax];
int g_putn;

void flush() {
	if (g_putn > 0)
		vfs::fd_write(1, g_put, (uint32_t)g_putn);
	g_putn = 0;
}

void put(char c) {
	if (g_putn >= kPutMax)
		flush();
	g_put[g_putn++] = c;

	if (c == '\n')
		flush();
}

} // namespace

void printf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print::vprintf(put, fmt, ap);
	va_end(ap);
	flush();
}

void print(const char* string) { printf("%s", string); }

} // namespace terminal
