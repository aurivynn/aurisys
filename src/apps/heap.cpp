#include "lib.h"

#include "lib/alloc.h"
#include "lib/vec.h"

int main(int argc, char** argv) {
	// flood grabs everything the arena will give and never gives it back
	if (argc > 1 && strcmp(argv[1], "flood") == 0) {
		size_t live = 0;
		int n = 0;
		void* last = nullptr;
		for (;;) {
			void* p = malloc(4096);
			if (!p)
				break;
			last = p;
			live += 4096;
			if (++n > 4096)
				break;
		}
		printf("flood took %u bytes in %d blocks\n", (unsigned)live, n);
		printf("break %p, last block ends %p\n", (void*)(uintptr_t)brk(0), (void*)((uint8_t*)last + 4096u));
		printf("flood used most of the arena %s\n", live > 600u * 1024u ? "OK" : "FAIL");
		printf("now refusing more %s\n", malloc(4096) == nullptr ? "OK" : "FAIL");
		return 0;
	}
	(void)argc;
	(void)argv;

	const uint32_t start = (uint32_t)brk(0);
	printf("brk starts at %08x\n", start);
	if (start == 0) {
		printf("no arena\n");
		return 1;
	}
	// grow nby hand
	if (brk(start + 4096u) == 0) {
		printf("brk refused to grow\n");
		return 1;
	}
	printf("brk after grow %08x\n", (uint32_t)brk(0));
	printf("sbrk +64 -> %08x\n", sbrk(64));
	printf("sbrk way past the end -> %08x (0 means refused)\n", sbrk(0x7FFFFFF0));

	uint32_t* a = (uint32_t*)malloc(64);
	uint32_t* b = (uint32_t*)malloc(64);
	if (!a || !b) {
		printf("malloc failed\n");
		return 1;
	}
	a[0] = 0xA5A5A5A5u;
	b[0] = 0x5A5A5A5Au;
	printf("malloc two blocks %s\n", (a != b && a[0] == 0xA5A5A5A5u && b[0] == 0x5A5A5A5Au) ? "OK" : "FAIL");

	free(a);
	uint32_t* again = (uint32_t*)malloc(64);
	printf("malloc after free %s\n", again ? "OK" : "FAIL");
	free(again);
	free(b);

	uint8_t* z = (uint8_t*)calloc(32, 8);
	bool zeroed = true;
	if (z)
		for (int i = 0; i < 256; ++i)
			if (z[i])
				zeroed = false;
	printf("calloc zeroes %s\n", zeroed ? "OK" : "FAIL");
	free(z);

	uint8_t* keep = (uint8_t*)malloc(0);
	*keep = 0x5A;
	for (int i = 0; i < 400; ++i) {
		uint8_t* t = (uint8_t*)malloc(64);
		if (!t) {
			printf("churn ran out at %d\n", i);
			break;
		}
		free(t);
	}
	printf("free coalesces %s\n", (keep && *keep == 0x5A) ? "OK" : "FAIL");
	free(keep);

	uint8_t* r = (uint8_t*)malloc(16);
	for (int i = 0; i < 16; ++i)
		r[i] = (uint8_t)(i + 1);
	r = (uint8_t*)realloc(r, 512);
	bool kept = true;
	if (!r)
		kept = false;
	else
		for (int i = 0; i < 16; ++i)
			if (r[i] != (uint8_t)(i + 1))
				kept = false;
	printf("realloc keeps data %s\n", kept ? "OK" : "FAIL");
	free(r);

	Vec<int> v;
	for (int i = 0; i < 100; ++i)
		v.push(i * 3);
	int sum = 0;
	for (size_t i = 0; i < v.size(); ++i)
		sum += v[i];
	printf("Vec<int> size=%u sum=%d (want 14850) %s\n", (unsigned)v.size(), sum, sum == 14850 ? "OK" : "FAIL");

	String s = "aurisys";
	s.push_back('!');
	printf("String is \"%s\" len=%u %s\n", s.c_str(), (unsigned)s.length(), s.size() == 8 ? "OK" : "FAIL");

	return 0;
}
