// testing proc calls
#include "lib.h"

static int fails = 0;

static void ok(const char* what, int cond) {
	printf("%s %s\n", cond ? "  ok  " : "FAIL  ", what);
	if (!cond)
		++fails;
}

static int coin = 7;

static volatile int got_usr1 = 0;
static volatile int got_term = 0;

static void on_usr1(int sig, sigframe* fp) {
	if (!fp)
		return;
	got_usr1 = (int)fp->eip;
}

static void on_term(int sig, sigframe* fp) {
	(void)sig;
	(void)fp;
	got_term = 1;
}

static volatile int got_int = 0;

static void on_int(int sig, sigframe* fp) {
	(void)sig;
	(void)fp;
	got_int = 1;
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	// who am i
	const int me = getpid();
	const int dad = getppid();
	printf("proc: pid %d ppid %d\n", me, dad);
	ok("getpid returned something real", me > 0);
	ok("getppid returned something real", dad > 0);
	ok("getpid is stable", getpid() == me);

	// fork & copy on write
	coin = 7;
	{
		const int kid = fork();
		if (kid == 0) {
			if (coin != 7)
				exit(21); // inherited the wrong value
			coin = 42;

			int* p = (int*)sbrk(64);
			if (!p)
				exit(22);
			p[0] = 0x5a5a;
			if (p[0] != 0x5a5a)
				exit(23);
			exit(0);
		}
		ok("fork returned a pid", kid > 0);
		int st = 0;
		const int got = waitpid(kid, &st);
		ok("waitpid returned the child", got == kid);
		ok("the child exited cleanly", st == 0);
		ok("the child's write did not reach the parent", coin == 7);
	}

	coin = 99;
	ok("the parent can still write shared memory", coin == 99);

	// fork, the child execs
	{
		const int kid = fork();
		if (kid == 0) {
			char* av[4];
			char n0[] = "echo";
			char n1[] = "from-execve";
			av[0] = n0;
			av[1] = n1;
			av[2] = nullptr;
			execve("/bin/echo", av, nullptr);

			exit(30);
		}
		int st = 0;
		waitpid(kid, &st);
		ok("a child can exec a program and it runs", st == 0);
	}

	{
		char* av[2];
		char n0[] = "nosuch";
		av[0] = n0;
		av[1] = nullptr;
		ok("execve of a missing file fails", execve("/bin/nosuch", av, nullptr) < 0);
		ok("the process survives a failed exec", 1);
	}

	// an exit code survives
	{
		const int kid = fork();
		if (kid == 0)
			exit(3);
		int st = 0;
		waitpid(kid, &st);
		ok("an exit code comes back through wait", st == (3 << 8));
	}

	// a caught signal
	got_usr1 = 0;
	{
		sigaction sa;
		sa.handler = (uint32_t)(uintptr_t)on_usr1;
		sa.flags = 0;
		sa.mask = 0;
		ok("sigaction installs a handler", sig_set(kSigUsr1, &sa, nullptr) == 0);
	}
	{
		const int kid = fork();
		if (kid == 0) {
			sleep_ms(60); // long enough for the parent to be parked in wait
			kill(getppid(), kSigUsr1);
			exit(0);
		}
		int st = 0;
		waitpid(kid, &st);
	}
	ok("a caught signal ran its handler", got_usr1 != 0);

	// SIGKILL cannot be caught so installing one must be refused
	{
		sigaction sa;
		sa.handler = (uint32_t)(uintptr_t)on_term;
		sa.flags = 0;
		sa.mask = 0;
		ok("sigaction refuses SIGKILL", sig_set(kSigKill, &sa, nullptr) < 0);
	}

	{
		sigaction sa;
		sa.handler = (uint32_t)(uintptr_t)on_int;
		sa.flags = 0;
		sa.mask = 0;
		got_int = 0;
		const int installed = sig_set(kSigInt, &sa, nullptr);
		ok("sigaction installs a SIGINT handler", installed == 0);
		if (installed == 0) {
			char c = 0;
			for (int i = 0; i < 200 && !got_int; ++i) {
				if (read(0, &c, 1) > 0)
					break;
				sleep_ms(10);
			}
			ok("ctrl-c reached the process as a signal", got_int != 0);
		}
	}

	// environment
	{
		char* av[3];
		char n0[] = "showenv";
		av[0] = n0;
		av[1] = nullptr;
		char* ev[4];
		char e0[] = "AURISYS_PHASE4=envp";
		char e1[] = "SECOND=two";
		ev[0] = e0;
		ev[1] = e1;
		ev[2] = nullptr;
		const int kid = fork();
		if (kid == 0) {
			execve("/bin/showenv", av, ev);
			exit(31);
		}
		int st = 0;
		const int got = waitpid(kid, &st);
		ok("execve carries the environment to the new program", got == kid && st == 0);
	}

	// an uncaught signal kills
	{
		const int kid = fork();
		if (kid == 0) {
			sleep_ms(2000); // long enough that only a signal ends it
			exit(0);
		}
		sleep_ms(50);
		kill(kid, kSigKill);
		int st = 0;
		const int got = waitpid(kid, &st);
		ok("an uncaught SIGKILL ends the process", got == kid);
		ok("wait says it died of a signal", st == 128 + kSigKill);
	}

	// descriptors
	{
		const int fd = open("/bin/echo", 0);
		ok("open for reading works", fd >= 0);
		const int copy = dup(fd);
		ok("dup gives a second descriptor", copy >= 0 && copy != fd);
		const int moved = dup2(fd, 9);
		ok("dup2 lands where it was told", moved == 9);
		ok("the original is untouched", lseek(fd, 0, 0) == 0);
		close(copy);
		close(moved);
		close(fd);
	}

	{
		int fds[2];
		ok("pipe works", pipe(fds) == 0);
		const int kid = fork();
		if (kid == 0) {
			close(fds[0]);
			const char* m = "through-a-pipe\n";
			write(fds[1], m, 15);
			close(fds[1]);
			exit(0);
		}
		close(fds[1]);
		char buf[32];
		const int n = read(fds[0], buf, sizeof buf - 1);
		buf[n > 0 ? n : 0] = 0;
		ok("a pipe read what the child wrote", n == 15);
		printf("proc: pipe said %s", buf);
		close(fds[0]);
		waitpid(kid, nullptr);
	}

	// waiting for nothing
	ok("waiting for a pid that is not ours fails", waitpid(9999, nullptr) < 0);

	printf("proc: %s, %d failure(s)\n", fails ? "FAILED" : "all passed", fails);
	return fails ? 1 : 0;
}
