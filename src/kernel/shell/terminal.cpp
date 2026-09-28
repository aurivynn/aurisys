// the terminal

#include "shell/terminal.h"

#include "drivers/console.h"
#include "drivers/fb.h"
#include "drivers/kbd.h"
#include "drivers/serial.h"
#include "exec.h"
#include "fs.h"
#include "lib/mem.h"
#include "lib/print.h"
#include "lib/str.h"
#include "lib/time.h"
#include "task.h"
#include "vfs.h"

#include <stdarg.h>
#include <stdint.h>

namespace terminal {

// environment
constexpr int kMaxEnv = 32;
constexpr int kEnvMax = 128;
static char g_env[kMaxEnv][kEnvMax];
static int g_env_n;

static bool env_is(const char* entry, const char* name) {
	int i = 0;
	while (name[i] && name[i] != '=') {
		if (entry[i] != name[i])
			return false;
		++i;
	}
	return entry[i] == '=';
}

void env_set(const char* text) {
	if (!text)
		return;
	int slot = -1;
	for (int i = 0; i < g_env_n; ++i)
		if (env_is(g_env[i], text)) {
			slot = i;
			break;
		}
	if (slot < 0) {
		if (g_env_n >= kMaxEnv)
			return;
		slot = g_env_n++;
	}
	strncpy(g_env[slot], text, kEnvMax - 1);
	g_env[slot][kEnvMax - 1] = 0;
}

void env_assign(const char* name, const char* value) {
	char buf[kEnvMax];
	if (!name)
		return;
	if (value) {
		const size_t nl = strlen(name);
		if (nl + strlen(value) + 2 >= sizeof buf)
			return;
		memcpy(buf, name, nl);
		buf[nl] = '=';
		strcpy(buf + nl + 1, value);
	} else {
		strncpy(buf, name, sizeof buf - 1);
		buf[sizeof buf - 1] = 0;
	}
	env_set(buf);
}

void env_unset(const char* name) {
	for (int i = 0; i < g_env_n; ++i) {
		if (!env_is(g_env[i], name))
			continue;

		for (int k = i + 1; k < g_env_n; ++k)
			strcpy(g_env[k - 1], g_env[k]);
		--g_env_n;
		return;
	}
}

int env_count() { return g_env_n; }

const char** env_vector() {
	static const char* v[kMaxEnv + 1];
	for (int i = 0; i < g_env_n; ++i)
		v[i] = g_env[i];
	v[g_env_n] = nullptr;
	return v;
}

const char* getenv_from_shell(const char* name) {
	if (!name)
		return nullptr;
	for (int i = 0; i < g_env_n; ++i)
		if (env_is(g_env[i], name))
			return strchr(g_env[i], '=') + 1;
	return nullptr;
}

bool set_env_in_shell(const char* name, const char* value) {
	if (!name || !*name)
		return false;
	env_assign(name, value);
	return true;
}

namespace {

const int kLineMax = 127; // +1 for the NUL
const int kHistMax = 32;  // entries in the command history

bool g_phase = false; // blink phase we last painted
bool g_painted = false;
int g_px = 0, g_py = 0;

char g_line[kLineMax + 1];
int g_n = 0;
int g_pos = 0;
int g_line_x = 0;
int g_line_y = 0;
int g_drawn = 0;

char g_hist[kHistMax][kLineMax + 1];
int g_hist_count = 0;
int g_browse = 0;
char g_saved[kLineMax + 1];

void cursor_hide() {
	if (!g_painted)
		return;
	fb::fill_rect(g_px * 8, g_py * 16, 8, 16, console::bg());
	g_painted = false;
}

void cursor_paint() {
	cursor_hide();
	if (!g_phase)
		return;
	fb::fill_rect(console::cx() * 8 + 1, console::cy() * 16 + 1, 6, 14, 0xCBA6F7);
	g_painted = true;
	g_px = console::cx();
	g_py = console::cy();
}

void cursor_tick() {
	const bool on = (time::ms() % 1000) < 500; // 500ms on, 500ms off
	if (on == g_phase)
		return;
	g_phase = on;
	cursor_paint();
}

void redraw_line() {
	cursor_hide();
	console::gotoxy(g_line_x, g_line_y);
	console::puts(g_line);
	if (g_drawn > g_n)
		for (int i = g_n; i < g_drawn; ++i)
			console::putchar(' ');
	g_drawn = g_n > g_drawn ? g_n : g_drawn;
	console::gotoxy(g_line_x + g_pos, g_line_y);
	cursor_paint();
}

void hist_add() {
	if (g_n == 0)
		return;
	if (g_hist_count > 0 && strcmp(g_hist[g_hist_count - 1], g_line) == 0)
		return; // no consecutive repeats
	if (g_hist_count < kHistMax) {
		strcpy(g_hist[g_hist_count], g_line);
		++g_hist_count;
	} else {
		for (int i = 1; i < kHistMax; ++i)
			strcpy(g_hist[i - 1], g_hist[i]);
		strcpy(g_hist[kHistMax - 1], g_line);
	}
}

void hist_load(int i) {
	strcpy(g_line, g_hist[i]);
	g_n = (int)strlen(g_line);
	g_pos = g_n;
	redraw_line();
}

void hist_up() {
	if (g_hist_count == 0)
		return;
	if (g_browse == g_hist_count) { // leaving the live edit
		strcpy(g_saved, g_line);
		g_browse = g_hist_count - 1;
	} else if (g_browse > 0) {
		--g_browse;
	}
	hist_load(g_browse);
}

void hist_down() {
	if (g_browse >= g_hist_count)
		return; // already back at the live edit
	++g_browse;
	if (g_browse == g_hist_count) {
		strcpy(g_line, g_saved); // restore what was being typed
		g_n = (int)strlen(g_line);
		g_pos = g_n;
		redraw_line();
	} else {
		hist_load(g_browse);
	}
}

// io
// one char to serial and the screen
void emit_both(char c) {
	serial::putc(c);
	console::putchar(c);
}

void fd1_put(char c) {
	const char b = c;
	vfs::fd_write(1, &b, 1);
}

void vprint(const char* fmt, va_list ap) { print::vprintf(fd1_put, fmt, ap); }

// wait for a key, keyboard first then serial
int read_char() {
	for (;;) {
		cursor_tick();

		int c = task::console_key();
		if (c >= 0)
			return c;
		kbd::wait();
	}
}

constexpr int kMaxStage = 4;
constexpr int kMaxWord = 8;
constexpr int kWordMax = 120;

constexpr int kMaxNest = 4;

using bank_t = char[kMaxStage][kMaxWord][kWordMax];

static bank_t g_bank[kMaxNest + 1];

static const char* lookup_var(const char* name, int len) {
	for (int i = 0; i < g_env_n; ++i) {
		const char* eq = strchr(g_env[i], '=');
		if (!eq || (int)(eq - g_env[i]) != len || strncmp(g_env[i], name, (size_t)len) != 0)
			continue;
		return eq + 1;
	}
	return nullptr;
}

struct redir {
	char kind; // 0 none, '>' truncate, 'a' append, '<' read
	char path[kWordMax];
};

struct stage {
	int argc;
	const char* word[kMaxWord];
	redir in, out;
};

struct scan {
	char* p;
};

static int run_job(char** line, char* op, bool go, int depth);
static bool builtin_cd(int argc, const char** argv);
static bool builtin_kill(int argc, const char** argv);
static bool builtin_export(int argc, const char** argv);
static bool builtin_env(int argc, const char** argv);
static bool builtin_exit(int argc, const char** argv);

static bool is_op(char c) { return c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '(' || c == ')'; }

static void skip_blanks(scan& s) {
	while (*s.p == ' ' || *s.p == '\t')
		++s.p;
}

static const char* match_bracket(const char* p) {
	int depth = 0;
	for (; *p; ++p) {
		if (*p == '(')
			++depth;
		else if (*p == ')' && --depth == 0)
			return p;
	}
	return nullptr;
}

static int substitute(const char* inner, int innern, char* out, int outsz, int depth) {
	static char cap[512];
	cap[0] = 0;
	const int m = vfs::fd_mem(cap, sizeof cap - 1);
	if (m < 0)
		return 0;
	char line[kLineMax + 1];
	int n = innern > kLineMax ? kLineMax : innern;
	for (int i = 0; i < n; ++i)
		line[i] = inner[i];
	line[n] = 0;

	const int stash = vfs::kMaxFd - 1 - depth;
	const int saved = vfs::dup2(1, stash);
	vfs::dup2(m, 1);
	char* q = line;
	char op = 0;
	run_job(&q, &op, true, depth);
	vfs::fd_close(m);
	if (saved >= 0) {
		vfs::dup2(saved, 1);
		vfs::fd_close(saved);
	}
	// the newline a command leaves at the end is not part of its answer
	int len = 0;
	while (len < (int)sizeof cap - 1 && cap[len])
		++len;
	while (len > 0 && (cap[len - 1] == '\n' || cap[len - 1] == '\r'))
		--len;
	int put = 0;
	for (int i = 0; i < len && put < outsz - 1; ++i)
		out[put++] = cap[i];
	return put;
}

static int expand_dollar(const char** pp, char* out, int outsz, int depth) {
	const char* p = *pp;
	if (p[1] == '(') {
		const char* close = match_bracket(p + 1);
		if (!close)
			return 0;
		*pp = close + 1;
		if (depth >= kMaxNest) {
			terminal::printf("substitution nested too deeply\n");
			return 0;
		}
		return substitute(p + 2, (int)(close - p - 2), out, outsz, depth + 1);
	}
	if ((p[1] >= 'a' && p[1] <= 'z') || (p[1] >= 'A' && p[1] <= 'Z') || p[1] == '_') {
		int len = 0;
		while ((p[1 + len] >= 'a' && p[1 + len] <= 'z') || (p[1 + len] >= 'A' && p[1 + len] <= 'Z') ||
			   (p[1 + len] >= '0' && p[1 + len] <= '9') || p[1 + len] == '_')
			++len;
		*pp = p + 1 + len;
		const char* v = lookup_var(p + 1, len);
		if (!v)
			return 0; // an unknown name is nothing at all
		int n = 0;
		for (; v[n] && n < outsz - 1; ++n)
			out[n] = v[n];
		return n;
	}
	*pp = p + 1;
	if (outsz > 1) {
		out[0] = '$';
		return 1;
	}
	return 0;
}

static const char* class_end(const char* p) {
	++p;
	if (*p == '!' || *p == '^')
		++p;
	if (*p == ']')
		++p;
	for (; *p; ++p)
		if (*p == ']')
			return p;
	return nullptr;
}

static bool class_match(const char** pp, char c) {
	const char* end = class_end(*pp);
	if (!end)
		return false;
	const char* p = *pp + 1;
	bool neg = false;
	if (*p == '!' || *p == '^') {
		neg = true;
		++p;
	}
	bool hit = false;
	for (; p < end; ++p) {
		if (p[1] == '-' && p + 2 < end) {
			if ((unsigned char)*p <= (unsigned char)c && (unsigned char)c <= (unsigned char)p[2])
				hit = true;
			p += 2;
			continue;
		}
		if (*p == c)
			hit = true;
	}
	*pp = end + 1;
	return neg ? !hit : hit;
}

static bool has_glob(const char* w) {
	for (const char* p = w; *p; ++p)
		if (*p == '*' || *p == '?')
			return true;
		else if (*p == '[' && class_end(p))
			return true;
	return false;
}

static bool glob_match(const char* pat, const char* s) {
	while (*pat) {
		if (*pat == '*') {
			++pat;
			if (!*pat)
				return true;
			for (const char* t = s;; ++t) {
				if (glob_match(pat, t))
					return true;
				if (!*t)
					return false;
			}
		}
		if (!*s)
			return false;
		if (*pat == '[' && class_end(pat)) {
			const char* q = pat;
			if (!class_match(&q, *s))
				return false;
			pat = q;
			++s;
			continue;
		}
		if (*pat != '?' && *pat != *s)
			return false;
		++pat;
		++s;
	}
	return !*s;
}

static int glob_stage(stage& cur, char words[kMaxWord][kWordMax], int slot) {
	if (!has_glob(words[slot]))
		return cur.argc;
	const char* w = words[slot];
	char dir[kWordMax];
	const char* pat = w;
	int dlen = 0;
	for (const char* q = w; *q; ++q)
		if (*q == '/') {
			pat = q + 1;
			dlen = (int)(q - w) + 1;
		}
	for (int i = 0; i < dlen && i < kWordMax - 1; ++i)
		dir[i] = w[i];
	dir[dlen] = 0;
	if (!dlen) {
		dir[0] = '.';
		dir[1] = 0;
	}
	vfs::node* d = vfs::resolve(dir);
	if (!d)
		return cur.argc;

	static char hits[kMaxWord][kWordMax];
	int nh = 0;
	for (uint32_t i = 0; i < 64 && nh < kMaxWord - slot; ++i) {
		vfs::node e;
		if (vfs::readdir(d, i, &e) != 0)
			break;
		// . and .. are never handed back by a glob, whatever they match
		if ((e.name[0] == '.' && (e.name[1] == 0 || (e.name[1] == '.' && e.name[2] == 0))) || !glob_match(pat, e.name))
			continue;
		int n = 0;
		for (int k = 0; dir[k] && n < kWordMax - 2; ++k)
			hits[nh][n++] = dir[k];
		for (const char* q = e.name; *q && n < kWordMax - 1; ++q)
			hits[nh][n++] = *q;
		hits[nh][n] = 0;
		++nh;
	}
	if (nh == 0)
		return cur.argc;

	for (int k = kMaxWord - 1; k >= slot + nh; --k)
		cur.word[k] = cur.word[k - nh];
	for (int k = 0; k < nh; ++k)
		cur.word[slot + k] = hits[k];
	cur.argc += nh - 1;
	return cur.argc;
}

static bool read_word(scan& s, char* out, int outsz, int depth) {
	skip_blanks(s);
	int n = 0;
	bool any = false;

	while (*s.p && *s.p != ' ' && *s.p != '\t' && !is_op(*s.p)) {
		any = true;
		if (*s.p == '\'') {
			++s.p;
			while (*s.p && *s.p != '\'' && n < outsz - 1)
				out[n++] = *s.p++;
			if (*s.p == '\'')
				++s.p;
			continue;
		}
		if (*s.p == '"') {
			++s.p;

			while (*s.p && *s.p != '"') {
				if (n >= outsz - 1)
					break;
				if (*s.p == '\\' && s.p[1]) {
					++s.p;
					out[n++] = *s.p++;
					continue;
				}
				if (*s.p == '$') {
					const char* q = s.p;
					n += expand_dollar(&q, out + n, outsz - n, depth);
					s.p = (char*)q;
					continue;
				}
				out[n++] = *s.p++;
			}
			if (*s.p == '"')
				++s.p;
			continue;
		}
		if (*s.p == '$') {
			any = true;
			const char* q = s.p;
			n += expand_dollar(&q, out + n, outsz - n, depth);
			s.p = (char*)q;
			continue;
		}
		if (*s.p == '\\' && s.p[1])
			++s.p;
		if (n < outsz - 1)
			out[n++] = *s.p++;
		else
			++s.p;
	}
	out[n] = 0;
	return any;
}

static char read_op(scan& s) {
	skip_blanks(s);
	if (!*s.p)
		return 0;
	const char c = *s.p;
	if ((c == '&' || c == '|' || c == '>') && s.p[1] == c)
		++s.p;
	++s.p;
	return c;
}

static int open_for(const redir& r, bool write) {
	if (!r.kind)
		return -1;
	if (!write)
		return vfs::fd_open(r.path, O_RDONLY);
	const uint32_t mode = O_WRONLY | O_CREAT | (r.kind == 'a' ? O_APPEND : O_TRUNC);
	return vfs::fd_open(r.path, mode);
}

int open_console() { return vfs::dup2(0, vfs::next_free()); }

enum which_result {
	kFound,
	kNoSuchCommand, // no such name on the search path
	kNotAProgram,	// it names something, and that something is not a program
	kNoSuchFile,
};

static which_result which(const char* name, char* out, int outsz) {
	const bool given_as_path = strchr(name, '/') != nullptr;

	if (vfs::find_in_path(name, out, outsz)) {
		const vfs::node* n = vfs::resolve(out);
		return (n && n->type == vfs::kTypeDir) ? kNotAProgram : kFound;
	}
	if (!given_as_path)
		return kNoSuchCommand;
	strncpy(out, name, outsz - 1);
	out[outsz - 1] = 0;
	const vfs::node* n = vfs::resolve(out);
	if (!n)
		return kNoSuchFile;
	return n->type == vfs::kTypeDir ? kNotAProgram : kFound;
}

static void report_not_runnable(const char* name, which_result why) {
	switch (why) {
	case kNotAProgram:
		terminal::printf("%s: not a program\n", name);
		break;
	case kNoSuchFile:
		terminal::printf("%s: no such file\n", name);
		break;
	default:
		terminal::printf("unknown command: %s\n", name);
		break;
	}
}

static int run_alone(stage& s) {
	uint32_t code = 0;
	char pp[256];
	const which_result why = which(s.word[0], pp, sizeof pp);
	if (why != kFound) {
		report_not_runnable(s.word[0], why);
		return 127;
	}
	int saved[2] = {-1, -1};
	if (s.in.kind) {
		const int fd = open_for(s.in, false);
		if (fd < 0) {
			terminal::printf("%s: %s: no such file\n", s.word[0], s.in.path);
			return 1;
		}
		saved[0] = vfs::dup2(0, 9);
		vfs::dup2(fd, 0);
		vfs::fd_close(fd);
	}
	if (s.out.kind) {
		const int fd = open_for(s.out, true);
		if (fd < 0) {
			terminal::printf("%s: %s: cannot write\n", s.word[0], s.out.path);
			return 1;
		}
		saved[1] = vfs::dup2(1, 10);
		vfs::dup2(fd, 1);
		vfs::fd_close(fd);
	}

	if (saved[0] >= 0 || saved[1] >= 0) {
		if (!exec::run(pp, s.argc, s.word, &code))
			code = 127;
		if (saved[0] >= 0) {
			vfs::dup2(saved[0], 0);
			vfs::fd_close(saved[0]);
		}
		if (saved[1] >= 0) {
			vfs::dup2(saved[1], 1);
			vfs::fd_close(saved[1]);
		}
	} else if (!exec::run(pp, s.argc, s.word, &code)) {
		code = 127;
	}
	return (int)code;
}

static int wait_for_pid(uint32_t pid) {
	task::task* t = nullptr;
	for (;;) {
		t = task::find(pid);
		if (!t || t->state == task::kZombie)
			break;
		task::yield();
	}
	const int code = t ? (int)t->exit_code : 0;
	const uint32_t sig = t ? t->signal : 0;
	task::clear_foreground(t);
	task::reap();
	task::take_signal(pid);
	return sig ? 128 + (int)sig : code;
}

static int run_pipeline(stage* st, int n, bool background) {
	int pf[kMaxStage - 1][2];
	uint32_t pids[kMaxStage];
	int npid = 0;
	int status = 0;

	for (int i = 0; i + 1 < n; ++i)
		if (vfs::pipe(pf[i])) {
			terminal::printf("pipe: no room for a pipe\n");
			return 1;
		}

	for (int i = 0; i < n; ++i) {
		stage& s = st[i];

		int infd = -1;
		if (i > 0)
			infd = pf[i - 1][0];
		else if (s.in.kind)
			infd = open_for(s.in, false);
		else
			infd = open_console();

		int outfd = -1;
		if (i + 1 < n)
			outfd = pf[i][1];
		else if (s.out.kind)
			outfd = open_for(s.out, true);

		if (i + 1 == n && !background) {
			const bool had_out = s.out.kind != 0;

			int saved[2] = {-1, -1};
			if (infd >= 0) {
				saved[0] = vfs::dup2(0, 9);
				vfs::dup2(infd, 0);
			}
			if (outfd >= 0) {
				saved[1] = vfs::dup2(1, 10);
				vfs::dup2(outfd, 1);
			}
			s.in.kind = 0;
			s.out.kind = 0;
			status = run_alone(s);
			if (saved[0] >= 0) {
				vfs::dup2(saved[0], 0);
				vfs::fd_close(saved[0]);
			}
			if (saved[1] >= 0) {
				vfs::dup2(saved[1], 1);
				vfs::fd_close(saved[1]);
			}

			if (infd >= 0)
				vfs::fd_close(infd);
			if (outfd >= 0 && had_out)
				vfs::fd_close(outfd);
			continue;
		}

		char pp[256];
		const which_result why = which(s.word[0], pp, sizeof pp);
		if (why != kFound) {
			report_not_runnable(s.word[0], why);
			for (int k = 0; k < n - 1; ++k) {
				vfs::fd_close(pf[k][0]);
				vfs::fd_close(pf[k][1]);
			}
			return 127;
		}
		exec::fdmap map[3];
		int nmap = 0;
		if (infd >= 0)
			map[nmap++] = {0, infd};
		if (outfd >= 0)
			map[nmap++] = {1, outfd};

		const uint32_t kid = exec::spawn_mapped(pp, s.argc, s.word, map, nmap, env_count(), env_vector());
		if (!kid) {
			terminal::printf("%s: could not start\n", s.word[0]);
			status = 127;
		}

		if (infd >= 0)
			vfs::fd_close(infd);
		if (outfd >= 0)
			vfs::fd_close(outfd);

		if (kid)
			pids[npid++] = kid;
		if (background) {
			terminal::printf("[%u] %s\n", kid, pp);
			task::set_foreground(task::find(kid));
		}
	}
	if (background)
		return 0;

	for (int i = 0; i < npid; ++i)
		wait_for_pid(pids[i]);
	return status;
}

static int run_job(char** line, char* op, bool go, int depth) {
	bank_t* bank = &g_bank[depth];
	stage st[kMaxStage];
	int nstage = 0;
	bool background = false;
	scan s = {*line};
	memset(st, 0, sizeof st);

	for (;;) {
		stage& cur = st[nstage];
		memset(&cur, 0, sizeof cur);

		int slot = 0;
		for (;;) {
			skip_blanks(s);
			if (!*s.p)
				break;
			char c = *s.p;

			if (c == '>' || c == '<') {
				const char kind = c;
				++s.p;
				if (kind == '>' && *s.p == '>') {
					++s.p; // >> appends
					cur.out.kind = 'a';
				} else if (kind == '<') {
					cur.in.kind = '<';
				} else {
					cur.out.kind = '>';
				}
				char* where = kind == '<' ? cur.in.path : cur.out.path;
				if (!read_word(s, where, kWordMax, depth))
					terminal::printf("syntax error: %c with nothing after it\n", kind);
				continue;
			}
			if (is_op(c) || c == '&')
				break;
			if (slot >= kMaxWord) {
				// too many words for one stage
				while (*s.p && !is_op(*s.p) && *s.p != '&')
					++s.p;
				continue;
			}
			if (read_word(s, (*bank)[nstage][slot], kWordMax, depth)) {
				cur.word[cur.argc++] = (*bank)[nstage][slot];
				++slot;
				slot = cur.argc = glob_stage(cur, (*bank)[nstage], slot - 1);
			}
		}
		skip_blanks(s);
		// a stage with no words is not a stage
		if (cur.argc == 0)
			break;
		++nstage;

		if (*s.p == '|' && s.p[1] != '|') {
			++s.p;
			if (nstage >= kMaxStage) {
				terminal::printf("too many stages in a pipeline\n");
				while (*s.p && *s.p != ';' && *s.p != '&')
					++s.p;
				break;
			}
			continue;
		}
		break;
	}

	if (*s.p == '&' && s.p[1] != '&') {
		background = true;
		++s.p;
	}
	*op = read_op(s);

	*line = s.p;
	if (nstage == 0)
		return 0;
	if (!go)
		return 0;

	if (nstage == 1 && !st[0].in.kind && !st[0].out.kind) {
		if (builtin_cd(st[0].argc, st[0].word))
			return 0;
		if (builtin_kill(st[0].argc, st[0].word))
			return 0;
		if (builtin_export(st[0].argc, st[0].word))
			return 0;
		if (builtin_env(st[0].argc, st[0].word))
			return 0;
		if (builtin_exit(st[0].argc, st[0].word))
			return 0;
	}
	return run_pipeline(st, nstage, background);
}

// cd stays a builtin because there is no chdir syscall yet
bool builtin_cd(int argc, const char** argv) {
	if (strcmp(argv[0], "cd") != 0)
		return false;
	if (argc > 2) {
		terminal::printf("usage: cd [dir]\n");
		return true;
	}
	const char* where = argc == 2 ? argv[1] : "/";
	if (!task::chdir(where))
		terminal::printf("cd: %s: not a directory\n", where);
	return true;
}

int match_ci(const char* a, const char* b) {
	while (*a && *b) {
		char x = *a, y = *b;
		if (x >= 'a' && x <= 'z')
			x = (char)(x - 'a' + 'A');
		if (y >= 'a' && y <= 'z')
			y = (char)(y - 'a' + 'A');
		if (x != y)
			return (int)(unsigned char)x - (int)(unsigned char)y;
		++a;
		++b;
	}
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

bool builtin_kill(int argc, const char** argv) {
	if (strcmp(argv[0], "kill") != 0)
		return false;
	if (argc < 2 || argc > 3) {
		terminal::printf("usage: kill <pid> [TERM|KILL|SEGV]\n");
		return true;
	}
	uint32_t sig = task::kSigTerm;
	if (argc == 3) {
		const char* s = argv[2];
		if (match_ci(s, "KILL") == 0)
			sig = task::kSigKill;
		else if (match_ci(s, "SEGV") == 0)
			sig = task::kSigSegv;
		else if (match_ci(s, "TERM") == 0)
			sig = task::kSigTerm;
		else {
			terminal::printf("kill: unknown signal %s\n", s);
			return true;
		}
	}
	if (argv[1][0] < '0' || argv[1][0] > '9') {
		terminal::printf("kill: %s is not a pid\n", argv[1]);
		return true;
	}
	uint32_t pid = 0;
	for (const char* p = argv[1]; *p >= '0' && *p <= '9'; ++p)
		pid = pid * 10u + (uint32_t)(*p - '0');

	if (pid == task::kPid1) {
		terminal::printf("kill: %u is the shell\n", pid);
		return true;
	}
	if (!task::kill(pid, sig))
		terminal::printf("kill: no such process %u\n", pid);
	else
		terminal::printf("kill: %u got %s\n", pid, task::signal_name(sig));
	return true;
}

bool builtin_export(int argc, const char** argv) {
	if (strcmp(argv[0], "export") != 0)
		return false;
	if (argc < 2 || argc > 3) {
		terminal::printf("usage: export NAME[=VALUE]\n");
		return true;
	}
	const char* eq = strchr(argv[1], '=');
	if (eq) {
		char name[32];
		const int nl = (int)(eq - argv[1]);
		if (nl > 30) {
			terminal::printf("export: %s: name too long\n", argv[1]);
			return true;
		}
		for (int i = 0; i < nl; ++i)
			name[i] = argv[1][i];
		name[nl] = 0;
		env_assign(name, eq + 1);
	} else {
		env_assign(argv[1], "");
	}
	return true;
}

bool builtin_env(int argc, const char** argv) {
	if (strcmp(argv[0], "env") != 0)
		return false;
	if (argc == 1) {
		for (int i = 0; i < g_env_n; ++i)
			terminal::printf("%s\n", g_env[i]);
		return true;
	}
	if (argc != 2) {
		terminal::printf("usage: env [NAME]\n");
		return true;
	}
	const char* v = getenv_from_shell(argv[1]);
	if (!v) {
		terminal::printf("env: %s not set\n", argv[1]);
		return true;
	}
	terminal::printf("%s=%s\n", argv[1], v);
	return true;
}

bool builtin_exit(int argc, const char** argv) {
	if (strcmp(argv[0], "exit") != 0)
		return false;
	int code = 0;
	if (argc >= 2) {
		code = 0;
		for (const char* p = argv[1]; *p >= '0' && *p <= '9'; ++p)
			code = code * 10 + (*p - '0');
	}

	terminal::printf("\n");
	terminal::printf("aurisys: halted\n");
	for (;;)
		asm volatile("cli; hlt");
}

void run_line(char* line) {
	int status = 0;
	bool go = true;
	for (;;) {
		char op = 0;
		status = run_job(&line, &op, go, 0);
		if (op == 0)
			break;

		if (op == ';')
			go = true;
		else if (op == '&')
			go = status == 0;
		else if (op == '|')
			go = status != 0;
		else
			break;
	}
}

void prompt() {
	console::setcolor(0xCBA6F7, 0x1E1E2E);
	terminal::printf("aurisys ");
	console::setcolor(0xA6E3A1, 0x1E1E2E);
	terminal::printf("%s", task::cwd());
	console::setcolor(0xCDD6F4, 0x1E1E2E);
	terminal::printf("> ");
	cursor_paint();
}

// tab completion
void splice_word(int s, int e, const char* ins, int in_n, char suffix) {
	char nl[kLineMax + 1];
	int n = 0;
	for (int i = 0; i < s && i < g_n; ++i)
		nl[n++] = g_line[i];
	for (int i = 0; i < in_n && n < kLineMax; ++i)
		nl[n++] = ins[i];
	if (suffix && n < kLineMax)
		nl[n++] = suffix;
	for (int i = e; i < g_n && n < kLineMax; ++i)
		nl[n++] = g_line[i];
	nl[n] = 0;
	strcpy(g_line, nl);
	g_n = n;
	g_pos = s + in_n + (suffix ? 1 : 0);
	redraw_line();
}

void candidates_done() {
	emit_both('\n');
	prompt();
	g_line_x = console::cx();
	g_line_y = console::cy();
	redraw_line();
}

char g_last_tab[kLineMax + 1];
int g_last_tab_n = -1;

void complete_command(int fw) {
	const char* names[32];
	char disk_names[32][64];
	int n = 0;
	int common_len = 0;
	auto consider = [&](const char* nm) {
		if (strncmp(g_line, nm, (size_t)fw) != 0)
			return;
		const int len = (int)strlen(nm);
		if (n == 0)
			common_len = len;
		else {
			int j = 0;
			while (j < common_len && nm[j] && nm[j] == names[0][j])
				++j;
			common_len = j;
		}
		names[n++] = nm;
	};
	const char* p = vfs::path();
	while (*p) {
		char dir[128];
		int dn = 0;
		while (*p && *p != ':' && dn < 127)
			dir[dn++] = *p++;
		dir[dn] = 0;
		if (*p == ':')
			++p;
		if (dn == 0)
			continue;
		vfs::node* dnode = vfs::resolve(dir);
		if (!dnode)
			continue;
		for (uint32_t i = 0; n < 32; ++i) {
			vfs::node out;
			if (vfs::readdir(dnode, i, &out) < 0)
				break;
			if (out.name[0] == '.' && (out.name[1] == 0 || (out.name[1] == '.' && out.name[2] == 0)))
				continue;
			strncpy(disk_names[n], out.name, sizeof disk_names[n] - 1);
			disk_names[n][sizeof disk_names[n] - 1] = 0;
			consider(disk_names[n]);
		}
	}

	if (n == 0)
		return;
	if (common_len > fw) {
		g_last_tab_n = -1;
		splice_word(0, fw, names[0], common_len, 0);
		return;
	}
	if (n < 2)
		return;
	if (g_last_tab_n == fw) {
		bool same = true;
		for (int i = 0; same && i < fw; ++i)
			if (g_last_tab[i] != g_line[i])
				same = false;
		if (same)
			return;
	}
	g_last_tab_n = fw;
	strncpy(g_last_tab, g_line, (size_t)fw);
	g_last_tab[fw] = 0;
	emit_both('\n');
	for (int i = 0; i < n; ++i)
		terminal::printf(" %s", names[i]);
	candidates_done();
}

void complete_path(int s, int e) {
	const int tlen = e - s;
	char pref[128];
	int pref_len = tlen;
	int keep = 0;
	for (int i = e - 1; i >= s; --i) {
		if (g_line[i] == '/') {
			keep = i - s + 1;
			pref_len = e - (i + 1);
			break;
		}
	}
	memcpy(pref, g_line + s + keep, (size_t)pref_len);
	pref[pref_len] = 0;

	char dir[256];
	if (keep) {
		char tmp[256];
		memcpy(tmp, g_line + s, (size_t)keep);
		tmp[keep] = 0;
		if (!fs::resolve(tmp, task::cwd(), dir, sizeof dir))
			return;
	} else {
		strncpy(dir, task::cwd(), sizeof dir - 1);
		dir[sizeof dir - 1] = 0;
	}
	vfs::node* dnode = vfs::resolve(dir);
	if (!dnode)
		return;

	struct pathctx {
		const char* pref;
		int pref_len;
		char names[16][64];
		uint8_t types[16];
		int n;
		int common_len;
	} m = {pref, pref_len, {}, {}, 0, 0};
	for (uint32_t i = 0; m.n < 16; ++i) {
		vfs::node out;
		if (vfs::readdir(dnode, i, &out) < 0)
			break;
		const char* name = out.name;
		if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
			continue;
		if (strncmp(name, m.pref, (size_t)m.pref_len) != 0)
			continue;
		const int len = (int)strlen(name);
		if (m.n == 0)
			m.common_len = len;
		else {
			int j = 0;
			while (j < m.common_len && name[j] && name[j] == m.names[0][j])
				++j;
			m.common_len = j;
		}
		m.types[m.n] = out.type;
		int c = len > 63 ? 63 : len;
		memcpy(m.names[m.n], name, (size_t)c);
		m.names[m.n][c] = 0;
		++m.n;
	}
	if (m.n == 0)
		return;

	char repl[kLineMax + 1];
	auto build = [&](const char* name, int name_len) -> int {
		int rn = 0;
		for (int i = 0; i < keep && rn < kLineMax; ++i)
			repl[rn++] = g_line[s + i];
		for (int i = 0; i < name_len && rn < kLineMax; ++i)
			repl[rn++] = name[i];
		repl[rn] = 0;
		return rn;
	};

	if (m.n == 1) {
		const char suffix = m.types[0] == 2 ? '/' : ' ';
		const int rn = build(m.names[0], (int)strlen(m.names[0]));
		g_last_tab_n = -1;
		splice_word(s, e, repl, rn, suffix);
		return;
	}
	if (m.common_len > pref_len) {
		const int rn = build(m.names[0], m.common_len);
		g_last_tab_n = -1;
		splice_word(s, e, repl, rn, 0);
		return;
	}
	if (g_last_tab_n == pref_len) {
		bool same = true;
		for (int i = 0; same && i < pref_len; ++i)
			if (g_last_tab[i] != pref[i])
				same = false;
		if (same)
			return;
	}
	g_last_tab_n = pref_len;
	strncpy(g_last_tab, pref, (size_t)pref_len);
	g_last_tab[pref_len] = 0;
	emit_both('\n');
	for (int i = 0; i < m.n && i < 16; ++i)
		terminal::printf(" %s%c", m.names[i], m.types[i] == 2 ? '/' : ' ');
	candidates_done();
}

void tab_complete() {
	int fw = 0;
	while (fw < g_n && g_line[fw] != ' ')
		++fw;
	if (g_pos <= fw) {
		complete_command(fw);
		return;
	}
	int s = 0;
	for (int i = 0; i < g_pos && i < g_n; ++i)
		if (g_line[i] == ' ')
			s = i + 1;
	int e = s;
	while (e < g_n && g_line[e] != ' ')
		++e;
	if (e == s)
		return;
	complete_path(s, e);
}

} // namespace

void printf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprint(fmt, ap);
	va_end(ap);
}

void print(const char* string) { printf(string); }

void run() {
	env_assign("PATH", "/bin");
	env_assign("HOME", "/");
	env_assign("TERM", "vt100");

	for (;;) {
		prompt();
		if (task::take_signal(0) == task::kSigChild)
			terminal::printf("[background process finished]\n");
		g_line_x = console::cx();
		g_line_y = console::cy();
		g_line[0] = 0;
		g_n = 0;
		g_pos = 0;
		g_drawn = 0;
		g_browse = g_hist_count;
		bool done = false;
		while (!done) {
			const int c = read_char();
			cursor_hide(); // the block must not eat what we draw next
			if (c == '\n' || c == '\r') {
				emit_both('\n');
				hist_add();
				done = true;
			} else if (c == 0x03) { // ctrl+c: cancel the line
				g_line[0] = 0;
				g_n = 0;
				g_pos = 0;
				emit_both('^');
				emit_both('C');
				emit_both('\n');
				done = true;
			} else if (c == 0x7F) { // ctrl+backspace: kill the whole line
				g_line[0] = 0;
				g_n = 0;
				g_pos = 0;
				redraw_line();
			} else if (c == '\b') { // backspace: delete before the cursor
				if (g_pos > 0) {
					memmove(g_line + g_pos - 1, g_line + g_pos, (size_t)(g_n - g_pos));
					--g_pos;
					--g_n;
					g_line[g_n] = 0;
				}
				redraw_line();
			} else if (c == kbd::KEY_DEL) { // delete: drop the char under the cursor
				if (g_pos < g_n) {
					memmove(g_line + g_pos, g_line + g_pos + 1, (size_t)(g_n - g_pos - 1));
					--g_n;
					g_line[g_n] = 0;
				}
				redraw_line();
			} else if (c == kbd::KEY_LEFT) {
				if (g_pos > 0)
					--g_pos;
				redraw_line();
			} else if (c == kbd::KEY_RIGHT) {
				if (g_pos < g_n)
					++g_pos;
				redraw_line();
			} else if (c == kbd::KEY_HOME) {
				g_pos = 0;
				redraw_line();
			} else if (c == kbd::KEY_END) {
				g_pos = g_n;
				redraw_line();
			} else if (c == kbd::KEY_UP) {
				hist_up();
			} else if (c == kbd::KEY_DOWN) {
				hist_down();
			} else if (c == '\t') {
				tab_complete();
			} else if (c >= ' ' && c <= '~' && g_n < kLineMax) {
				memmove(g_line + g_pos + 1, g_line + g_pos, (size_t)(g_n - g_pos));
				g_line[g_pos] = (char)c;
				++g_pos;
				++g_n;
				g_line[g_n] = 0;
				redraw_line();
			}
		}
		run_line(g_line);
	}
}

} // namespace terminal