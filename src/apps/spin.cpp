#include "lib.h"

// spins for a while so there is something to look at in /proc lol

int main(int argc, char** argv) {
	bool quiet = false;
	int first = 1;
	if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'q') {
		quiet = true;
		first = 2;
	}
	unsigned rounds = 40;
	if (argc > first) {
		rounds = 0;
		for (int i = 0; argv[first][i] >= '0' && argv[first][i] <= '9'; ++i)
			rounds = rounds * 10u + (unsigned)(argv[first][i] - '0');
		if (rounds == 0)
			rounds = 1;
	}
	if (!quiet)
		printf("spin %u rounds\n", rounds);
	for (unsigned i = 0; i < rounds; ++i) {
		const int want = uptime_ms() + 100;
		while (uptime_ms() < want) {
		}
	}
	if (!quiet)
		printf("spin done\n");
	return 0;
}
