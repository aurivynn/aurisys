#include "apps/app.h"

#include "lib/time.h"
#include "shell/terminal.h"

namespace apps {

int uptime_main(int argc, const char** argv) {
	(void)argc;
	(void)argv;
	terminal::printf("uptime: %ums since boot\n", time::ms());
	return 0;
}

} // namespace apps