// shows the environment this program was started with.

#include "lib.h"

// -c NAME=VALUE means this must be set to exactly this
// -c NAME means this must not be set at all

static int check(const char* want) {
	const char* eq = strchr(want, '=');
	if (!eq) {
		if (getenv(want)) {
			printf("showenv: %s is set but should not be\n", want);
			return 1;
		}
		return 0;
	}

	// PATH and PATH2 are different variables so the comparison stops at the '='
	char name[64];
	const size_t n = (size_t)(eq - want);
	if (n >= sizeof name) {
		printf("showenv: name too long\n");
		return 1;
	}
	memcpy(name, want, n);
	name[n] = 0;

	const char* got = nullptr;
	for (char** e = environ; *e && !got; ++e) {
		if (strncmp(*e, name, n) == 0 && (*e)[n] == '=')
			got = *e + n + 1;
	}
	if (!got) {
		printf("showenv: %s is not set\n", name);
		return 1;
	}
	if (strcmp(got, eq + 1) != 0) {
		printf("showenv: %s is %s, expected %s\n", name, got, eq + 1);
		return 1;
	}
	return 0;
}

int main(int argc, char** argv) {
	if (argc > 1 && strcmp(argv[1], "-c") == 0) {
		if (argc == 2) {
			printf("usage: showenv -c NAME=VALUE | NAME ...\n");
			return 2;
		}
		int bad = 0;
		for (int i = 2; i < argc; ++i)
			bad |= check(argv[i]);
		return bad ? 1 : 0;
	}

	if (argc == 1) {
		for (char** e = environ; *e; ++e)
			printf("%s\n", *e);
		return 0;
	}

	int bad = 0;
	for (int i = 1; i < argc; ++i) {
		const char* v = getenv(argv[i]);
		if (!v) {
			printf("showenv: %s is not set\n", argv[i]);
			bad = 1;
		} else {
			printf("%s=%s\n", argv[i], v);
		}
	}
	return bad;
}
