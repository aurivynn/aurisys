#include "apps/app.h"

#include "shell/terminal.h"

namespace apps {

int help_main(int argc, const char** argv) {
	(void)argc;
	(void)argv;
	terminal::printf("apps:\n");
	for (int i = 0; i < count(); ++i) {
		const app& a = table()[i];
		terminal::printf("  %s    %s\n", a.name, a.desc);
	}
	return 0;
}

} // namespace apps