#include "lib.h"

int main(int argc, char** argv) {
	if (argc < 2) {
		printf("usage: rmdir <path> [path...]\n");
		return 1;
	}
	for (int i = 1; i < argc; ++i) {
		const int r = rmdir(argv[i]);
		if (r == 1) {
			printf("rmdir: %s: is a mount point\n", argv[i]);
			return 1;
		}
		if (r != 0) {
			printf("rmdir: %s: failed (missing? not empty? not a directory?)\n", argv[i]);
			return 1;
		}
	}
	return 0;
}
