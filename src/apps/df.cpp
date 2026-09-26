#include "lib.h"

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	fs_stat s;
	statfs(&s);
	printf("block size   %u\n", s.block_size);
	printf("blocks       %u   free %u\n", s.blocks, s.free_blocks);
	printf("inodes       %u   free %u\n", s.inodes, s.free_inodes);
	printf("extents      %s\n", (s.feat_incompat & 0x40) ? "yes" : "no");
	printf("features     compat=%x  incompat=%x  ro=%x\n", s.feat_compat, s.feat_incompat, s.feat_ro);
	return 0;
}