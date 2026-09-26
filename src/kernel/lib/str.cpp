#include "lib/str.h"

size_t strlen(const char* s) {
	size_t n = 0;
	while (s[n])
		++n;
	return n;
}

int strcmp(const char* a, const char* b) {
	while (*a && *a == *b) {
		++a;
		++b;
	}
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		if (a[i] != b[i])
			return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
		if (!a[i])
			return 0;
	}
	return 0;
}

char* strcpy(char* dst, const char* src) {
	char* d = dst;
	while ((*d++ = *src++)) {
	}
	return dst;
}

char* strncpy(char* dst, const char* src, size_t n) {
	char* d = dst;
	while (n && *src) {
		*d++ = *src++;
		--n;
	}
	while (n--) {
		*d++ = 0;
	}
	return dst;
}

char* strcat(char* dst, const char* src) {
	char* d = dst;
	while (*d)
		++d;
	while ((*d++ = *src++)) {
	}
	return dst;
}

char* strchr(const char* s, char c) {
	for (; *s; ++s)
		if (*s == c)
			return (char*)s;
	return nullptr;
}

char* strstr(const char* hay, const char* needle) {
	if (!*needle)
		return (char*)hay;
	for (; *hay; ++hay) {
		size_t i = 0;
		while (hay[i] && hay[i] == needle[i])
			++i;
		if (!needle[i])
			return (char*)hay;
	}
	return nullptr;
}