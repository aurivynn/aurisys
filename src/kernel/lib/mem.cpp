#include "lib/mem.h"

#include <stdint.h>

// byte loops. slow but it works.

extern "C" {

void* memcpy(void* dst, const void* src, size_t n) {
	uint8_t* d = (uint8_t*)dst;
	const uint8_t* s = (const uint8_t*)src;
	for (size_t i = 0; i < n; ++i)
		d[i] = s[i];
	return dst;
}

void* memset(void* dst, int c, size_t n) {
	uint8_t* d = (uint8_t*)dst;
	const uint8_t v = (uint8_t)c;
	for (size_t i = 0; i < n; ++i)
		d[i] = v;
	return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
	uint8_t* d = (uint8_t*)dst;
	const uint8_t* s = (const uint8_t*)src;
	if (d < s) {
		for (size_t i = 0; i < n; ++i)
			d[i] = s[i];
	} else if (d > s) {
		for (size_t i = n; i-- > 0;)
			d[i] = s[i]; // backwards
	}
	return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
	const uint8_t* x = (const uint8_t*)a;
	const uint8_t* y = (const uint8_t*)b;
	for (size_t i = 0; i < n; ++i) {
		if (x[i] != y[i])
			return (int)x[i] - (int)y[i];
	}
	return 0;
}

} // extern "C"