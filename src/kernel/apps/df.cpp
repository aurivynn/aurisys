#include "apps/app.h"

#include "fs.h"
#include "shell/terminal.h"

namespace apps {

int df_main(int argc, const char** argv) {
	(void)argc;
	(void)argv;
	uint32_t bs, blocks, free_b, inodes, free_i, compat, incompat, ro;
	fs::summary(&bs, &blocks, &free_b, &inodes, &free_i, &compat, &incompat, &ro);
	terminal::printf("block size   %u\n", bs);
	terminal::printf("blocks       %u   free %u\n", blocks, free_b);
	terminal::printf("inodes       %u   free %u\n", inodes, free_i);
	terminal::printf("extents      %s\n", (incompat & 0x40) ? "yes" : "no");
	terminal::printf("features     compat=%x  incompat=%x  ro=%x\n", compat, incompat, ro);
	return 0;
}

} // namespace apps