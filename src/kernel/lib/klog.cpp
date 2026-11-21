#include "lib/klog.h"

#include "drivers/console.h"
#include "drivers/serial.h"
#include "lib/print.h"

#include <stdarg.h>

namespace klog {

void putc(char c) {
	serial::putc(c);
	console::putchar(c);
}

void printf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print::vprintf(&putc, fmt, ap);
	va_end(ap);
}

} // namespace klog
