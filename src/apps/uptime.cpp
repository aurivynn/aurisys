#include "lib.h"

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	printf("uptime: %ums since boot\n", (unsigned)uptime_ms());
	return 0;
}