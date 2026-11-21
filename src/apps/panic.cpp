#include "lib.h"

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	sys_call(SYS_panic);
	return 0;
}