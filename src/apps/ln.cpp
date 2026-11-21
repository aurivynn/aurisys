#include "lib.h"

int main(int argc, char** argv) {
	bool soft = false;
	int first = 1;
	if (argc >= 2 && argv[1][0] == '-' && argv[1][1] == 's' && argv[1][2] == 0) {
		soft = true;
		first = 2;
	}
	if (argc != first + 2) {
		printf("usage: ln [-s] <target> <linkname>\n");
		return 1;
	}
	const int r = soft ? symlink(argv[first], argv[first + 1]) : link(argv[first], argv[first + 1]);
	if (r == 0)
		return 0;
	if (soft)
		printf("ln: cannot create symbolic link '%s' -> '%s'\n", argv[first + 1], argv[first]);
	else
		printf("ln: cannot create hard link '%s' -> '%s'\n", argv[first + 1], argv[first]);
	return 1;
}
