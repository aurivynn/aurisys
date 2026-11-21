#include "lib.h"

int main(int argc, char** argv) {
	if (argc != 2) {
		printf("usage: readlink <path>\n");
		return 1;
	}
	char buf[256];
	const int n = readlink(argv[1], buf, sizeof buf);
	if (n < 0) {
		printf("readlink: %s: not a symbolic link\n", argv[1]);
		return 1;
	}
	printf("%s\n", buf);
	return 0;
}
