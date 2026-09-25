#include "apps/app.h"

#include "shell/terminal.h"

namespace apps {

int echo_main(int argc, const char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (i > 1)
			terminal::printf(" ");
		terminal::printf("%s", argv[i]);
	}
	terminal::printf("\n");
	return 0;
}

} // namespace apps