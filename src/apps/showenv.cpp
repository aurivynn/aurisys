// reports the environment it was started with
#include "lib.h"

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	const char* want = "AURISYS_PHASE4";
	const char* got = getenv(want);
	if (!got) {
		printf("showenv: %s is not set\n", want);
		return 1;
	}
	printf("showenv: %s=%s\n", want, got);
	if (strcmp(got, "envp") != 0) {
		printf("showenv: wrong value\n");
		return 1;
	}

	const char* two = getenv("SECOND");
	if (!two || strcmp(two, "two") != 0) {
		printf("showenv: SECOND missing\n");
		return 1;
	}

	if (getenv("NOT_PASSED_AT_ALL")) {
		printf("showenv: invented a variable\n");
		return 1;
	}
	printf("showenv: ok\n");
	return 0;
}
