#include "lib.h"

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	asm volatile("int $0x80" ::"a"((uint32_t)SYS_panic) : "memory");
	return 0;
}