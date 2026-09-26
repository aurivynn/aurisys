#include "lib.h"

int main(int argc, char** argv) {
	if (argc < 2) {
		printf("usage: rm <path> [path...]\n");
		return 1;
	}
	for (int i = 1; i < argc; ++i) {
		const int r = rm(argv[i]);
		if (r == 1) {
			printf("rm: %s: is a mount point\n", argv[i]);
			return 1;
		}
		if (r != 0) {
			printf("rm: %s: failed (missing? not empty?)\n", argv[i]);
			return 1;
		}
	}
	return 0;
}