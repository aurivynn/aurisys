#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static const char* kLog = "serial.log";
static const char* kSock = "/tmp/aurisys_test.sock";

static pid_t g_qemu = -1;
static bool g_ok = true;

static long now_ms() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sleep_ms(int ms) {
	struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
	nanosleep(&ts, nullptr);
}

static void check(const char* label, bool cond) {
	if (!cond) {
		g_ok = false;
		printf("FAIL: %s\n", label);
	}
}

static std::string read_log() {
	FILE* f = fopen(kLog, "rb");
	if (!f)
		return "";
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::string s;
	if (n > 0) {
		s.resize((size_t)n);
		fread(&s[0], 1, (size_t)n, f);
	}
	fclose(f);
	return s;
}

static bool wait_for(const char* text, int timeout_ms) {
	const long deadline = now_ms() + timeout_ms;
	while (now_ms() < deadline) {
		if (read_log().find(text) != std::string::npos)
			return true;
		sleep_ms(100);
	}
	return false;
}

static void launch_qemu() {
	unlink(kLog);
	unlink(kSock);

	const pid_t pid = fork();
	if (pid == 0) {
		const int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, 1);
			dup2(devnull, 2);
			close(devnull);
		}
		execlp("qemu-system-i386", "qemu-system-i386", "-display", "none", "-drive",
			   "format=raw,file=build/aurisys.img", "-vga", "std", "-serial", "file:serial.log", "-monitor",
			   "unix:/tmp/aurisys_test.sock,server,nowait", "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
			   "-no-reboot", (char*)nullptr);
		perror("exec qemu");
		_exit(127);
	}
	g_qemu = pid;
}

static int connect_monitor() {
	const long deadline = now_ms() + 15000;
	while (now_ms() < deadline) {
		const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd >= 0) {
			struct sockaddr_un addr;
			memset(&addr, 0, sizeof(addr));
			addr.sun_family = AF_UNIX;
			strncpy(addr.sun_path, kSock, sizeof(addr.sun_path) - 1);
			if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0)
				return fd;
			close(fd);
		}
		sleep_ms(100);
	}
	return -1;
}

static void send_mon(int fd, const char* key) {
	std::string cmd = std::string("sendkey ") + key + "\n";
	send(fd, cmd.data(), cmd.size(), 0);
	sleep_ms(60);
}

static void type_keys(int fd, const char* const* keys, int n) {
	for (int i = 0; i < n; ++i)
		send_mon(fd, keys[i]);
}

static void stop_qemu() {
	if (g_qemu <= 0)
		return;
	kill(g_qemu, SIGTERM);
	int st;
	waitpid(g_qemu, &st, 0);
}

int main() {
	signal(SIGPIPE, SIG_IGN);

	launch_qemu();
	check("terminal never ready", wait_for("AURISYS: terminal ready", 15000));

	const int mon = connect_monitor();
	if (mon < 0) {
		check("monitor socket", false);
	} else {
		// app system + keyboard irq
		const char* help[] = {"h", "e", "l", "p"};
		type_keys(mon, help, 4);
		send_mon(mon, "ret");
		check("help app reply", wait_for("list the apps", 8000));

		// timer: the irq0 clock advances
		sleep_ms(1200);
		const char* uptime[] = {"u", "p", "t", "i", "m", "e"};
		type_keys(mon, uptime, 6);
		send_mon(mon, "ret");
		check("uptime app reply", wait_for("since boot", 8000));

		// heap allocator stats
		const char* mem[] = {"m", "e", "m"};
		type_keys(mon, mem, 3);
		send_mon(mon, "ret");
		check("mem app reply", wait_for("heap total=", 8000));

		// panic: div by zero -> idt -> dump, qemu exits
		const char* panic[] = {"p", "a", "n", "i", "c"};
		type_keys(mon, panic, 5);
		send_mon(mon, "ret");
		check("panic dump", wait_for("PANIC: exception", 12000));
		close(mon);
	}

	// boot assertions, straight off the same log
	const std::string log = read_log();
	check("vbe mode", log.find("1280x960 MODE OK") != std::string::npos);
	check("kernel tests", log.find("AURISYS: all tests passed") != std::string::npos);
	check("idt check", log.find("idt=OK") != std::string::npos);
	check("timer check", log.find("timer=OK") != std::string::npos);
	check("heap check", log.find("heap=OK") != std::string::npos);

	stop_qemu();

	if (!g_ok) {
		printf("BOOT TEST FAILED\n");
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}