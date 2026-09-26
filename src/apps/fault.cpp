#include "lib.h"

#include "syscall.h"

#include <stdint.h>

// touch an address the kernel never mapped
// ring 3 has no business reading it, so this is a page fault the kernel turns into the death of
// this process and nothing else
int main(int argc, char** argv) {
	uint32_t addr = 0x1000; // unmapped, and supervisor only besides
	if (argc > 1) {
		addr = 0;
		for (int i = 0; argv[1][i]; ++i) {
			const char c = argv[1][i];
			uint32_t d;
			if (c >= '0' && c <= '9')
				d = (uint32_t)(c - '0');
			else if (c >= 'a' && c <= 'f')
				d = (uint32_t)(c - 'a') + 10u;
			else if (c >= 'A' && c <= 'F')
				d = (uint32_t)(c - 'A') + 10u;
			else
				return 1;
			addr = (addr << 4) | d;
		}
	}
	printf("about to touch %08x\n", addr);
	volatile uint32_t* p = (volatile uint32_t*)(uintptr_t)addr;
	const uint32_t got = *p; // fault Here ok
	printf("survived, read %08x\n", got);
	return 0;
}
