#include "apps/app.h"

#include "lib/heap.h"
#include "shell/terminal.h"

#include <stddef.h>

namespace apps {

int mem_main(int argc, const char** argv) {
	(void)argc;
	(void)argv;
	size_t total = 0, used = 0, free = 0;
	heap_stats(&total, &used, &free);
	terminal::printf("heap total=%u used=%u free=%u\n", (uint32_t)total, (uint32_t)used, (uint32_t)free);
	return 0;
}

} // namespace apps