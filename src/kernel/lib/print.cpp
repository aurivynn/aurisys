#include "lib/print.h"

#include <stdint.h>

namespace print {

namespace {

const char kDigits[] = "0123456789ABCDEF";

int to_digits(char* out, uint32_t v, uint32_t base) {
	int n = 0;
	if (v == 0)
		out[n++] = '0';
	while (v) {
		out[n++] = kDigits[v % base];
		v /= base;
	}
	return n;
}

void emit_number(emit_fn emit, char* dig, int n, int width, bool zero, bool left, char sign) {
	int total = n + (sign ? 1 : 0);
	if (sign)
		emit(sign);
	if (total < width && !left) {
		const int pad = width - total;
		for (int i = 0; i < pad; ++i)
			emit(zero ? '0' : ' ');
	}
	for (int i = n; i-- > 0;)
		emit(dig[i]);
	if (left)
		for (int p = total; p < width; ++p)
			emit(' ');
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

		// flags: '-', '0', then a width
		bool left = false, zero = false;
		int width = 0;
		for (;;) {
			if (*fmt == '-') {
				left = true;
				++fmt;
			} else if (*fmt == '0') {
				zero = true;
				++fmt;
			} else if (*fmt >= '1' && *fmt <= '9') {
				width = width * 10 + (*fmt - '0');
				++fmt;
			} else {
				break;
			}
		}

		char dig[12];
		switch (*fmt) {
		case 'd':
		case 'i': {
			const int v = va_arg(ap, int);
			char sign = 0;
			uint32_t u;
			if (v < 0) {
				sign = '-';
				u = 0u - (uint32_t)v;
			} else {
				u = (uint32_t)v;
			}
			const int n = to_digits(dig, u, 10);
			emit_number(emit, dig, n, width, zero, left, sign);
			break;
		}
		case 'u': {
			const int n = to_digits(dig, va_arg(ap, uint32_t), 10);
			emit_number(emit, dig, n, width, zero, left, 0);
			break;
		}
		case 'x':
		case 'X': {
			const int n = to_digits(dig, va_arg(ap, uint32_t), 16);
			emit_number(emit, dig, n, width, zero, left, 0);
			break;
		}
		case 'p':
			emit('0');
			emit('x');
			emit_number(emit, dig, to_digits(dig, va_arg(ap, uint32_t), 16), 8, 1, 0, 0);
			break;
		case 'l':
			++fmt;
			if (*fmt == 'x' || *fmt == 'X') {
				const uint64_t v = va_arg(ap, uint64_t);
				emit('0');
				emit('x');
				for (int i = 15; i >= 0; --i)
					emit(kDigits[(v >> (i * 4)) & 0xFu]);
			}
			break;
		case 's': {
			const char* s = va_arg(ap, const char*);
			if (!s)
				s = "(null)";
			int len = 0;
			while (s[len])
				++len;
			if (len < width && !left)
				for (int p = len; p < width; ++p)
					emit(' ');
			while (*s)
				emit(*s++);
			if (left)
				for (int p = len; p < width; ++p)
					emit(' ');
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