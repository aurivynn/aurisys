#include "lib.h"

int main(int argc, char** argv) {
	if (argc != 2) {
		printf("usage: mkdir <path>\n");
		return 1;
	}
	if (mkdir(argv[1]) != 0) {
		printf("mkdir: %s: failed (exists? bad parent?)\n", argv[1]);
		return 1;
	}
	return 0;
}