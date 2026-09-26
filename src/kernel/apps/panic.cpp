#include "apps/app.h"

namespace apps {

int panic_main(int argc, const char** argv) {
	(void)argc;
	(void)argv;
	volatile int zero = 0;
	const int x = 5 / zero; // #DE, vector 0
	return x;
}

} // namespace apps