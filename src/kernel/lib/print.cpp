#include "lib/print.h"

#include <stdint.h>

namespace print {

namespace {

const char kDigits[] = "0123456789ABCDEF";

void emit_u32(emit_fn emit, uint32_t v, uint32_t base) {
	char buf[12];
	int i = 0;
	if (v == 0) {
		emit('0');
		return;
	}
	while (v != 0) {
		buf[i++] = kDigits[v % base];
		v /= base;
	}
	while (i)
		emit(buf[--i]);
}

// shifts, not div.
void emit_u64_hex(emit_fn emit, uint64_t v) {
	for (int i = 15; i >= 0; --i) {
		emit(kDigits[(v >> (i * 4)) & 0xFu]);
	}
}

} // namespace

void vprintf(emit_fn emit, const char* fmt, va_list ap) {
	for (; *fmt; ++fmt) {
		if (*fmt != '%') {
			emit(*fmt);
			continue;
		}
		++fmt;
		if (*fmt == '\0')
			return;
		switch (*fmt) {
		case 'd':
		case 'i': {
			const int v = va_arg(ap, int);
			uint32_t u;
			if (v < 0) {
				emit('-');
				u = 0u - (uint32_t)v;
			} else {
				u = (uint32_t)v;
			}
			emit_u32(emit, u, 10);
			break;
		}
		case 'u':
			emit_u32(emit, va_arg(ap, uint32_t), 10);
			break;
		case 'x':
			emit_u32(emit, va_arg(ap, uint32_t), 16);
			break;
		case 'p':
			emit('0');
			emit('x');
			emit_u32(emit, va_arg(ap, uint32_t), 16);
			break;
		case 'l':
			++fmt;
			if (*fmt == 'x' || *fmt == 'X') {
				const uint64_t v = va_arg(ap, uint64_t);
				emit('0');
				emit('x');
				emit_u64_hex(emit, v);
			}
			break;
		case 's': {
			const char* s = va_arg(ap, const char*);
			if (!s)
				s = "(null)";
			while (*s)
				emit(*s++);
			break;
		}
		case 'c':
			emit((char)va_arg(ap, int));
			break;
		case '%':
			emit('%');
			break;
		default:
			emit('%');
			emit(*fmt);
			break;
		}
	}
}

} // namespace print