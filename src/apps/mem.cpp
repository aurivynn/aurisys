#include "lib.h"

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	mem_stat m;
	meminfo(&m);
	printf("heap total=%u used=%u free=%u\n", m.total, m.used, m.free);
	return 0;
}