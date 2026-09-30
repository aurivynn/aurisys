// the shell
#include "lib.h"

#include <stdint.h>

// output
int g_tty = -1;

char g_out[1024];
int g_outn;

void flush_out() {
	if (g_outn > 0)
		write(g_tty, g_out, (uint32_t)g_outn);
	g_outn = 0;
}

void emit(char c) {
	if (g_outn >= (int)sizeof g_out)
		flush_out();
	g_out[g_outn++] = c;
}

const char* kDim = "\x1b[38;2;137;220;235m";	// the path and the sign
const char* kBright = "\x1b[38;2;203;166;247m"; // the name
const char* kPlain = "\x1b[0m";

const char* kUp = "\x1b[A";
const char* kDown = "\x1b[B";
const char* kRight = "\x1b[C";
const char* kLeft = "\x1b[D";
const char* kHome = "\x1b[H";
const char* kEnd = "\x1b[F";

void say(const char* s) {
	while (*s)
		emit(*s++);
}

void sayn(const char* s, int n) {
	for (int i = 0; i < n; ++i)
		emit(s[i]);
}

void sh_printf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	print::vprintf(emit, fmt, ap);
	va_end(ap);
	flush_out();
}

// environment
constexpr int kMaxEnv = 32;
constexpr int kEnvMax = 128;

static char g_env[kMaxEnv][kEnvMax];
static int g_env_n;

static char* g_envp[kMaxEnv + 1];

static bool env_is(const char* entry, const char* name) {
	int i = 0;
	while (name[i] && name[i] != '=') {
		if (entry[i] != name[i])
			return false;
		++i;
	}
	return entry[i] == '=';
}

static const char* env_get(const char* name) {
	for (int i = 0; i < g_env_n; ++i)
		if (env_is(g_env[i], name)) {
			const char* eq = strchr(g_env[i], '=');
			return eq ? eq + 1 : "";
		}
	return nullptr;
}

static void env_assign(const char* name, const char* value) {
	if (!name || !*name)
		return;
	int slot = -1;
	for (int i = 0; i < g_env_n; ++i)
		if (env_is(g_env[i], name)) {
			slot = i;
			break;
		}
	if (slot < 0) {
		if (g_env_n >= kMaxEnv)
			return; // no room
		slot = g_env_n++;
	}
	const size_t nl = strlen(name);
	if (nl + strlen(value) + 2 >= kEnvMax)
		return; // would be truncated
	strcpy(g_env[slot], name);
	g_env[slot][nl] = '=';
	strcpy(g_env[slot] + nl + 1, value);
}

static void env_set(const char* name_equals_value) {
	const char* eq = strchr(name_equals_value, '=');
	if (!eq) {
		env_assign(name_equals_value, "");
		return;
	}
	char name[64];
	const size_t n = (size_t)(eq - name_equals_value);
	if (n >= sizeof name)
		return;
	memcpy(name, name_equals_value, n);
	name[n] = 0;
	env_assign(name, eq + 1);
}

static void env_unset(const char* name) {
	for (int i = 0; i < g_env_n; ++i) {
		if (!env_is(g_env[i], name))
			continue;
		for (int k = i + 1; k < g_env_n; ++k)
			strcpy(g_env[k - 1], g_env[k]);
		--g_env_n;
		return;
	}
}

// the vector handed to execve
static char** env_vector() {
	for (int i = 0; i < g_env_n; ++i)
		g_envp[i] = g_env[i];
	g_envp[g_env_n] = nullptr;
	return g_envp;
}

// the terminal
enum {
	kKeyChar = 0,
	kKeyUp,
	kKeyDown,
	kKeyLeft,
	kKeyRight,
	kKeyHome,
	kKeyEnd,
	kKeyDel,
	kKeyIns,
	kKeyPgUp,
	kKeyPgDn,
	kKeyTab,
	kKeyEnter,
	kKeyBackspace,
	kKeyEof,
	kKeyNone,
};

int g_key;
char g_keych;

char g_in[64];
int g_inn;
int g_inhead;

void pump_input() {
	char c;
	while (g_inn < (int)sizeof g_in && read(g_tty, &c, 1) == 1)
		g_in[g_inn++] = c;
}

int next_key() {
	if (g_inhead >= g_inn) {
		g_inn = 0;
		g_inhead = 0;
		return 0;
	}
	const char c = g_in[g_inhead++];

	if ((uint8_t)c != 0x1b) {
		g_keych = c;
		if (c == '\n' || c == '\r')
			g_key = kKeyEnter;
		else if (c == '\t')
			g_key = kKeyTab;
		else if (c == 0x7F || c == 8)
			g_key = kKeyBackspace;
		else if (c == 0x04)
			g_key = kKeyEof;
		else
			g_key = kKeyChar;
		return 1;
	}

	char seq[8];
	int n = 0;
	seq[n++] = c;
	while (n < (int)sizeof seq && g_inhead < g_inn)
		seq[n++] = g_in[g_inhead++];
	if (n < 3 || seq[1] != '[') {
		g_key = kKeyChar;
		g_keych = n > 1 ? seq[1] : 0;
		return 1;
	}
	switch (seq[2]) {
	case 'A':
		g_key = kKeyUp;
		break;
	case 'B':
		g_key = kKeyDown;
		break;
	case 'C':
		g_key = kKeyRight;
		break;
	case 'D':
		g_key = kKeyLeft;
		break;
	case 'H':
		g_key = kKeyHome;
		break;
	case 'F':
		g_key = kKeyEnd;
		break;
	case '~':
		switch (n >= 4 ? seq[3] : 0) {
		case '2':
			g_key = kKeyIns;
			break;
		case '3':
			g_key = kKeyDel;
			break;
		case '5':
			g_key = kKeyPgUp;
			break;
		case '6':
			g_key = kKeyPgDn;
			break;
		default:
			g_key = kKeyNone;
			break;
		}
		break;
	default:
		g_key = kKeyNone;
		break;
	}
	return 1;
}

// line editing

constexpr int kLineMax = 256;
constexpr int kHistMax = 32;

static char g_line[kLineMax + 1];
static int g_n;
static int g_pos;
static int g_drawn;

static char g_hist[kHistMax][kLineMax + 1];
static int g_hist_count;
static int g_browse;
static char g_saved[kLineMax + 1];

static char g_prompt[256];
static int g_prompt_n;

void place_cursor() {
	say("\r");
	sayn(g_prompt, g_prompt_n);
	if (g_pos > 0)
		sayn(g_line, g_pos);
	flush_out();
}

void redraw_line() {
	say("\r");
	sayn(g_prompt, g_prompt_n);
	sayn(g_line, g_n);
	// erase whatever the longer previous line left behind
	for (int i = g_n; i < g_drawn; ++i)
		emit(' ');
	if (g_n > g_drawn)
		g_drawn = g_n;
	place_cursor();
	flush_out();
}

void line_set(const char* s) {
	strncpy(g_line, s, kLineMax);
	g_line[kLineMax] = 0;
	g_n = (int)strlen(g_line);
	g_pos = g_n;
	redraw_line();
}

void hist_add() {
	if (g_n == 0)
		return;
	if (g_hist_count > 0 && strcmp(g_hist[g_hist_count - 1], g_line) == 0)
		return; // no consecutive repeats
	if (g_hist_count < kHistMax) {
		strcpy(g_hist[g_hist_count++], g_line);
		return;
	}
	for (int i = 1; i < kHistMax; ++i)
		strcpy(g_hist[i - 1], g_hist[i]);
	strcpy(g_hist[kHistMax - 1], g_line);
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
	line_set(g_hist[g_browse]);
}

void hist_down() {
	if (g_browse >= g_hist_count)
		return; // already back at what was being typed
	++g_browse;
	if (g_browse == g_hist_count)
		line_set(g_saved);
	else
		line_set(g_hist[g_browse]);
}

void insert_char(char c) {
	if (g_n >= kLineMax)
		return;
	for (int i = g_n; i > g_pos; --i)
		g_line[i] = g_line[i - 1];
	g_line[g_pos++] = c;
	g_line[g_n++] = c;
	g_line[g_n] = 0;
	redraw_line();
}

void delete_back() {
	if (g_pos == 0)
		return;
	--g_pos;
	for (int i = g_pos; i < g_n; ++i)
		g_line[i] = g_line[i + 1];
	--g_n;
	g_line[g_n] = 0;
	redraw_line();
}

void delete_forward() {
	if (g_pos >= g_n)
		return;
	for (int i = g_pos; i + 1 < g_n; ++i)
		g_line[i] = g_line[i + 1];
	--g_n;
	g_line[g_n] = 0;
	redraw_line();
}

void move_cursor(int delta) {
	const int want = g_pos + delta;
	if (want < 0 || want > g_n)
		return;
	g_pos = want;
	place_cursor();
	flush_out();
}

// completion

void splice_word(int s, int e, const char* ins, int in_n) {
	char nl[kLineMax + 1];
	int n = 0;
	for (int i = 0; i < s && i < g_n; ++i)
		nl[n++] = g_line[i];
	for (int i = 0; i < in_n && n < kLineMax; ++i)
		nl[n++] = ins[i];
	for (int i = e; i < g_n && n < kLineMax; ++i)
		nl[n++] = g_line[i];
	nl[n] = 0;
	strcpy(g_line, nl);
	g_n = n;
	g_pos = s + in_n;
	redraw_line();
}

void redraw_after_listing() {
	say("\r\n");
	sayn(g_prompt, g_prompt_n);
	redraw_line();
}

char g_last_tab[kLineMax + 1];
int g_last_tab_n = -1;

bool tab_worth_listing(int prefix_n) {
	if (g_last_tab_n != prefix_n)
		return true;
	for (int i = 0; i < prefix_n; ++i)
		if (g_last_tab[i] != g_line[i])
			return true;
	return false;
}

void remember_tab(int prefix_n) {
	g_last_tab_n = prefix_n;
	strncpy(g_last_tab, g_line, (size_t)prefix_n);
	g_last_tab[prefix_n] = 0;
}

// shared prefix of two names
int common_of(const char* a, const char* b, int upto) {
	int j = 0;
	while (j < upto && a[j] && b[j] && a[j] == b[j])
		++j;
	return j;
}

void complete_command(int fw) {
	const char* names[64];
	char disk[64][64];
	int n = 0;
	int common_len = 0;

	const char* p = env_get("PATH");
	if (!p)
		p = "/bin";
	while (*p && n < 64) {
		char dir[128];
		int dn = 0;
		while (*p && *p != ':' && dn < (int)sizeof dir - 1)
			dir[dn++] = *p++;
		dir[dn] = 0;
		if (*p == ':')
			++p;
		if (dn == 0)
			continue;
		const int fd = open(dir, 0);
		if (fd < 0)
			continue;
		for (uint32_t i = 0; n < 64; ++i) {
			vfs_dirent e;
			if (readdir(fd, i, &e) < 0)
				break;
			if (is_dot(e.name))
				continue;
			strncpy(disk[n], e.name, sizeof disk[0] - 1);
			disk[n][sizeof disk[0] - 1] = 0;
			if (strncmp(g_line, disk[n], (size_t)fw) != 0)
				continue;
			const int len = (int)strlen(disk[n]);
			common_len = n == 0 ? len : common_of(names[0], disk[n], len);
			names[n] = disk[n];
			++n;
		}
		close(fd);
	}

	if (n == 0)
		return;
	if (common_len > fw) {
		g_last_tab_n = -1;
		splice_word(0, fw, names[0], common_len);
		return;
	}
	if (n < 2 || !tab_worth_listing(fw))
		return;
	remember_tab(fw);
	say("\r\n");
	for (int i = 0; i < n; ++i)
		sh_printf(" %s", names[i]);
	redraw_after_listing();
}

void complete_path(int s, int e) {
	const int len = e - s;
	char word[kLineMax + 1];
	for (int i = 0; i < len; ++i)
		word[i] = g_line[s + i];
	word[len] = 0;

	// split into the directory to look in and the pattern to match
	char dir[128];
	int dn = 0;
	const char* pat = word;
	for (const char* q = word; *q; ++q)
		if (*q == '/') {
			pat = q + 1;
			dn = (int)(q - word) + 1;
		}
	if (dn > (int)sizeof dir - 1)
		return;
	for (int i = 0; i < dn; ++i)
		dir[i] = word[i];
	dir[dn] = 0;
	if (!dn) {
		dir[0] = '.';
		dir[1] = 0;
	}

	const int fd = open(dir, 0);
	if (fd < 0)
		return;
	const char* names[32];
	uint8_t types[32];
	char store[32][128];
	int m = 0;
	for (uint32_t i = 0; m < 32; ++i) {
		vfs_dirent de;
		if (readdir(fd, i, &de) < 0)
			break;
		if (is_dot(de.name))
			continue;
		if (strncmp(de.name, pat, strlen(pat)) != 0)
			continue;
		strncpy(store[m], de.name, sizeof store[0] - 1);
		store[m][sizeof store[0] - 1] = 0;
		types[m] = de.type;
		names[m] = store[m];
		++m;
	}
	close(fd);
	if (m == 0)
		return;

	if (m == 1) {
		char full[256];
		int fn = 0;
		for (int i = 0; i < dn && fn < (int)sizeof full - 2; ++i)
			full[fn++] = dir[i];
		for (const char* q = names[0]; *q && fn < (int)sizeof full - 2; ++q)
			full[fn++] = *q;
		full[fn] = 0;
		splice_word(s, e, full, fn);
		return;
	}

	if (!tab_worth_listing(len))
		return;
	remember_tab(len);
	say("\r\n");
	for (int i = 0; i < m; ++i)
		sh_printf(" %s%c", names[i], types[i] == kTypeDir ? '/' : ' ');
	redraw_after_listing();
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

// parsing

constexpr int kMaxStage = 4;
constexpr int kMaxWord = 8;
constexpr int kWordMax = 120;
constexpr int kMaxNest = 4;

using bank_t = char[kMaxStage][kMaxWord][kWordMax];
static bank_t g_bank[kMaxNest + 1];

struct redir {
	char kind; // 0 none, '>' truncate, 'a' append, '<' read
	char path[kWordMax];
};

struct stage {
	int argc;
	char* word[kMaxWord];
	redir in, out;
};

struct scan {
	char* p;
};

static int run_job(char** line, char* op, bool go, int depth);

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

// $(...) runs a command and keeps what it printed
static int substitute(const char* inner, int innern, char* out, int outsz, int depth) {
	int ends[2];
	if (pipe(ends) < 0)
		return 0;

	char line[kLineMax + 1];
	int n = innern > kLineMax ? kLineMax : innern;
	for (int i = 0; i < n; ++i)
		line[i] = inner[i];
	line[n] = 0;

	const int kid = fork();
	if (kid == 0) {
		// the child writes into the pipe and is not the shell any more
		close(ends[0]);
		dup2(ends[1], 1);
		if (ends[1] != 1)
			close(ends[1]);
		char* q = line;
		char op = 0;
		run_job(&q, &op, true, depth);
		exit(0);
	}

	close(ends[1]);
	int put = 0;
	char buf[256];
	for (;;) {
		const int got = read(ends[0], buf, sizeof buf);
		if (got <= 0)
			break;
		for (int i = 0; i < got && put < outsz - 2; ++i)
			out[put++] = buf[i];
	}
	close(ends[0]);
	if (kid > 0)
		waitpid(kid, nullptr, 0);

	while (put > 0 && (out[put - 1] == '\n' || out[put - 1] == '\r'))
		--put;
	out[put] = 0;
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
			sh_printf("substitution nested too deeply\n");
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
		char name[64];
		if (len >= (int)sizeof name)
			return 0;
		memcpy(name, p + 1, (size_t)len);
		name[len] = 0;
		const char* v = env_get(name);
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
	const int fd = open(dir, 0);
	if (fd < 0)
		return cur.argc;

	static char hits[kMaxWord][kWordMax];
	int nh = 0;
	for (uint32_t i = 0; i < 64 && nh < kMaxWord - slot; ++i) {
		vfs_dirent e;
		if (readdir(fd, i, &e) < 0)
			break;
		// . and .. are never handed back by a glob, whatever they match
		if (is_dot(e.name) || !glob_match(pat, e.name))
			continue;
		int n = 0;
		for (int k = 0; dir[k] && n < kWordMax - 2; ++k)
			hits[nh][n++] = dir[k];
		for (const char* q = e.name; *q && n < kWordMax - 1; ++q)
			hits[nh][n++] = *q;
		hits[nh][n] = 0;
		++nh;
	}
	close(fd);
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
	if ((c == '&' || c == '|') && s.p[1] == c)
		++s.p;
	++s.p;
	return c;
}

// job control
struct job {
	int pid;
	int pgid;
	bool running;
	bool background;
	int status;
	char name[64];
};

constexpr int kMaxJobs = 8;
static job g_jobs[kMaxJobs];

constexpr int kMaxFg = 8;
static int g_fg[kMaxFg];
static int g_nfg;

volatile int g_interrupted;

static void fg_add(int pid) {
	if (pid <= 0)
		return;
	for (int i = 0; i < g_nfg; ++i)
		if (g_fg[i] == pid)
			return;
	if (g_nfg < kMaxFg)
		g_fg[g_nfg++] = pid;
}

static void fg_drop(int pid) {
	for (int i = 0; i < g_nfg; ++i)
		if (g_fg[i] == pid)
			g_fg[i] = 0;
}

static void fg_clear() {
	for (int i = 0; i < g_nfg; ++i)
		g_fg[i] = 0;
	g_nfg = 0;
}

static void on_interrupt(int sig, sigframe* fp) {
	(void)sig;
	(void)fp;
	g_interrupted = 1;
	for (int i = 0; i < g_nfg; ++i)
		if (g_fg[i] > 0)
			kill(g_fg[i], kSigInt);
}

void forget_jobs() {
	int live = 0;
	for (int i = 0; i < g_nfg; ++i)
		if (g_fg[i] > 0)
			++live;
	if (!live)
		fg_clear();
}

int find_job(int pid) {
	for (int i = 0; i < kMaxJobs; ++i)
		if (g_jobs[i].running && g_jobs[i].pid == pid)
			return i;
	return -1;
}

int free_job() {
	for (int i = 0; i < kMaxJobs; ++i)
		if (!g_jobs[i].running)
			return i;
	return -1;
}

int wait_pid(int pid) {
	if (pid <= 0)
		return 0;
	int st = 0;
	bool gone = false;
	for (;;) {
		const int got = waitpid(pid, &st, 0);
		if (got == pid)
			break;

		if (got == -kErrIntr)
			continue;

		if (got < 0) {
			gone = true;
			break;
		}
	}

	const int k = find_job(pid);
	if (k >= 0) {
		g_jobs[k].running = false;
		g_jobs[k].status = gone ? 0 : st;
	}
	fg_drop(pid);
	return gone ? 0 : st;
}

// running

enum which_result { kFound, kNoSuchCommand, kNotAProgram, kNoSuchFile };

static which_result which(const char* name, char* out, int outsz) {
	const bool given_as_path = strchr(name, '/') != nullptr;

	if (!given_as_path) {
		const char* p = env_get("PATH");
		if (!p)
			p = "/bin";
		while (*p) {
			char dir[128];
			int dn = 0;
			while (*p && *p != ':' && dn < (int)sizeof dir - 1)
				dir[dn++] = *p++;
			dir[dn] = 0;
			if (*p == ':')
				++p;
			if (dn == 0)
				continue;

			int fn = 0;
			for (int i = 0; dir[i] && fn < outsz - 2; ++i)
				out[fn++] = dir[i];
			if (fn > 0 && out[fn - 1] != '/')
				out[fn++] = '/';
			for (const char* q = name; *q && fn < outsz - 1; ++q)
				out[fn++] = *q;
			out[fn] = 0;
			const int fd = open(out, 0);
			if (fd >= 0) {
				file_stat st;
				const bool dir = fstat(fd, &st) == 0 && st.type == kTypeDir;
				close(fd);
				if (dir)
					return kNotAProgram;
				return kFound;
			}
		}
		return kNoSuchCommand;
	}

	strncpy(out, name, (size_t)outsz - 1);
	out[outsz - 1] = 0;
	const int fd = open(out, 0);
	if (fd < 0)
		return kNoSuchFile;
	file_stat st;
	const bool dir = fstat(fd, &st) == 0 && st.type == kTypeDir;
	close(fd);
	return dir ? kNotAProgram : kFound;
}

static void report_not_runnable(const char* name, which_result why) {
	switch (why) {
	case kNotAProgram:
		sh_printf("%s: not a program\n", name);
		break;
	case kNoSuchFile:
		sh_printf("%s: no such file\n", name);
		break;
	default:
		sh_printf("unknown command: %s\n", name);
		break;
	}
}

static int open_for(const redir& r, bool write) {
	if (!r.kind)
		return -1;
	if (!write)
		return open(r.path, 0);
	const uint32_t mode = O_WRONLY | O_CREAT | (r.kind == 'a' ? O_APPEND : O_TRUNC);
	return open(r.path, mode);
}

static int open_redir(const char* cmd, const redir& r, bool write) {
	const int fd = open_for(r, write);
	if (fd >= 0)
		return fd;
	if (write)
		sh_printf("%s: %s: cannot write\n", cmd, r.path);
	else
		sh_printf("%s: %s: no such file\n", cmd, r.path);
	return -1;
}

static void note_job(int pid, const char* name, bool background) {
	const int k = free_job();
	if (k < 0)
		return;
	g_jobs[k].pid = pid;
	g_jobs[k].running = true;
	g_jobs[k].background = background;
	g_jobs[k].status = 0;
	strncpy(g_jobs[k].name, name, sizeof g_jobs[k].name - 1);
	g_jobs[k].name[sizeof g_jobs[k].name - 1] = 0;
}

static int run_stage(stage& s, const char* path, int in, int out) {
	if (in < 0 && !s.in.kind)
		in = dup(0);
	if (out < 0 && !s.out.kind)
		out = dup(1);

	const int kid = fork();
	if (kid == 0) {
		if (in >= 0 && in != 0) {
			dup2(in, 0);
			close(in);
		}
		if (out >= 0 && out != 1) {
			dup2(out, 1);
			close(out);
		}
		char* av[kMaxWord + 1];
		for (int i = 0; i < s.argc; ++i)
			av[i] = s.word[i];
		av[s.argc] = nullptr;
		execve(path, av, env_vector());

		sh_printf("%s: could not start\n", s.word[0]);
		exit(127);
	}

	if (in >= 0)
		close(in);
	if (out >= 0)
		close(out);
	return kid;
}

static int run_job(char** line, char* op, bool go, int depth);

static bool builtin_cd(int argc, char** argv) {
	if (strcmp(argv[0], "cd") != 0)
		return false;
	if (argc > 2) {
		sh_printf("usage: cd [dir]\n");
		return true;
	}
	const char* where = argc == 2 ? argv[1] : "/";
	if (chdir(where) < 0)
		sh_printf("cd: %s: not a directory\n", where);
	return true;
}

static bool match_ci(const char* a, const char* b) {
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

static bool builtin_kill(int argc, char** argv) {
	if (strcmp(argv[0], "kill") != 0)
		return false;
	if (argc < 2 || argc > 3) {
		sh_printf("usage: kill <pid> [TERM|KILL|SEGV]\n");
		return true;
	}
	uint32_t sig = kSigTerm;
	if (argc == 3) {
		const char* s = argv[2];
		if (match_ci(s, "KILL") == 0)
			sig = kSigKill;
		else if (match_ci(s, "SEGV") == 0)
			sig = kSigSegv;
		else if (match_ci(s, "TERM") == 0)
			sig = kSigTerm;
		else if (match_ci(s, "INT") == 0)
			sig = kSigInt;
		else {
			sh_printf("kill: unknown signal %s\n", s);
			return true;
		}
	}
	if (argv[1][0] < '0' || argv[1][0] > '9') {
		sh_printf("kill: %s is not a pid\n", argv[1]);
		return true;
	}
	uint32_t pid = 0;
	for (const char* p = argv[1]; *p >= '0' && *p <= '9'; ++p)
		pid = pid * 10u + (uint32_t)(*p - '0');
	if (pid == 1) {
		sh_printf("kill: %u is the shell\n", pid);
		return true;
	}
	if (kill((int)pid, sig) < 0)
		sh_printf("kill: no such process %u\n", pid);
	else
		sh_printf("kill: %u got signal %u\n", pid, sig);
	return true;
}

static bool builtin_export(int argc, char** argv) {
	if (strcmp(argv[0], "export") != 0)
		return false;
	if (argc < 2 || argc > 3) {
		sh_printf("usage: export NAME[=VALUE]\n");
		return true;
	}
	env_set(argv[1]);
	return true;
}

static bool builtin_unset(int argc, char** argv) {
	if (strcmp(argv[0], "unset") != 0)
		return false;
	if (argc != 2) {
		sh_printf("usage: unset NAME\n");
		return true;
	}
	env_unset(argv[1]);
	return true;
}

static bool builtin_env(int argc, char** argv) {
	if (strcmp(argv[0], "env") != 0)
		return false;
	if (argc == 1) {
		for (int i = 0; i < g_env_n; ++i)
			sh_printf("%s\n", g_env[i]);
		return true;
	}
	if (argc != 2) {
		sh_printf("usage: env [NAME]\n");
		return true;
	}
	const char* v = env_get(argv[1]);
	if (!v) {
		sh_printf("env: %s not set\n", argv[1]);
		return true;
	}
	sh_printf("%s=%s\n", argv[1], v);
	return true;
}

static bool builtin_jobs(int argc, char** argv) {
	(void)argc;
	(void)argv;
	if (strcmp(argv[0], "jobs") != 0)
		return false;
	int any = 0;
	for (int i = 0; i < kMaxJobs; ++i) {
		if (!g_jobs[i].running)
			continue;
		++any;
		sh_printf("[%d] %-8s %s\n", g_jobs[i].pid, g_jobs[i].background ? "background" : "running", g_jobs[i].name);
	}
	if (!any)
		sh_printf("no jobs\n");
	return true;
}

static bool builtin_exit(int argc, char** argv) {
	if (strcmp(argv[0], "exit") != 0)
		return false;
	int code = 0;
	if (argc >= 2)
		for (const char* p = argv[1]; *p >= '0' && *p <= '9'; ++p)
			code = code * 10 + (*p - '0');

	if (getpid() == 1) {
		sh_printf("sh: pid 1 is the only process here. there is nothing to hand over to.\n");
		return true;
	}

	exit(code);
	return true;
}

static bool run_builtin(stage& s) {
	if (builtin_cd(s.argc, s.word))
		return true;
	if (builtin_kill(s.argc, s.word))
		return true;
	if (builtin_export(s.argc, s.word))
		return true;
	if (builtin_unset(s.argc, s.word))
		return true;
	if (builtin_env(s.argc, s.word))
		return true;
	if (builtin_jobs(s.argc, s.word))
		return true;
	if (builtin_exit(s.argc, s.word))
		return true;
	return false;
}

// one stage on its own which is a job of a single command
static int run_one(stage& s) {
	int in = s.in.kind ? open_redir(s.word[0], s.in, false) : -1;
	if (s.in.kind && in < 0)
		return 1;
	int out = s.out.kind ? open_redir(s.word[0], s.out, true) : -1;
	if (s.out.kind && out < 0) {
		if (in >= 0)
			close(in);
		return 1;
	}

	char pp[256];
	const which_result why = which(s.word[0], pp, sizeof pp);
	if (why != kFound) {
		report_not_runnable(s.word[0], why);
		if (in >= 0)
			close(in);
		if (out >= 0)
			close(out);
		return 127;
	}

	// run_stage takes ownership of both descriptors
	const int kid = run_stage(s, pp, in, out);
	if (kid <= 0)
		return 127;
	note_job(kid, s.word[0], false);
	fg_add(kid);
	const int status = wait_pid(kid);
	forget_jobs();
	return status;
}

// a job of several stages joined by pipes
static int run_pipeline(stage* st, int n, bool background) {
	if (n == 1)
		return run_one(st[0]);

	int pids[kMaxStage];
	int npid = 0;
	int status = 0;
	int carried = -1; // a read end waiting for the next stage

	for (int i = 0; i < n; ++i) {
		stage& s = st[i];

		int out = -1;
		int next_in = -1;
		if (i + 1 < n) {
			int ends[2];
			if (pipe(ends) < 0) {
				sh_printf("pipe: no room for a pipe\n");
				if (carried >= 0)
					close(carried);
				for (int k = 0; k < npid; ++k)
					wait_pid(pids[k]);
				return 1;
			}
			out = ends[1];
			next_in = ends[0];
		}

		int in = carried;
		carried = -1;
		if (in < 0 && s.in.kind) {
			in = open_redir(s.word[0], s.in, false);
			if (in < 0) {
				if (out >= 0)
					close(out);
				if (next_in >= 0)
					close(next_in);
				for (int k = 0; k < npid; ++k)
					wait_pid(pids[k]);
				return 1;
			}
		}
		if (out < 0 && s.out.kind) {
			out = open_redir(s.word[0], s.out, true);
			if (out < 0) {
				if (in >= 0)
					close(in);
				if (next_in >= 0)
					close(next_in);
				for (int k = 0; k < npid; ++k)
					wait_pid(pids[k]);
				return 1;
			}
		}

		char pp[256];
		const which_result why = which(s.word[0], pp, sizeof pp);
		if (why != kFound) {
			report_not_runnable(s.word[0], why);
			if (in >= 0)
				close(in);
			if (out >= 0)
				close(out);
			if (next_in >= 0)
				close(next_in);
			for (int k = 0; k < npid; ++k)
				wait_pid(pids[k]);
			return 127;
		}

		const int kid = run_stage(s, pp, in, out);
		carried = next_in;
		if (kid > 0) {
			pids[npid++] = kid;
			note_job(kid, s.word[0], background);
			if (!background)
				fg_add(kid);
		}
	}

	if (carried >= 0)
		close(carried);

	if (background) {
		sh_printf("[%d] %s\n", pids[npid - 1], st[n - 1].word[0]);
		return 0;
	}

	// every stage has to finish before the next prompt
	for (int i = 0; i < npid; ++i) {
		const int one = wait_pid(pids[i]);
		if (i == npid - 1)
			status = one;
	}
	forget_jobs();
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
			const char c = *s.p;

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
					sh_printf("syntax error: %c with nothing after it\n", kind);
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
				sh_printf("too many stages in a pipeline\n");
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

	if (nstage == 1 && !st[0].in.kind && !st[0].out.kind && run_builtin(st[0]))
		return 0;
	return run_pipeline(st, nstage, background);
}

static void run_line(char* line) {
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

// main
static void set_prompt() {
	char cwd[128];
	if (getcwd(cwd, sizeof cwd) < 0)
		strcpy(cwd, "/");

	int n = 0;
	const char* parts[5] = {kBright, "aurisys ", kDim, cwd, kPlain};
	for (int i = 0; i < 5; ++i)
		for (const char* q = parts[i]; *q && n < (int)sizeof g_prompt - 1; ++q)
			g_prompt[n++] = *q;

	const char* tail[2] = {"> ", kPlain};
	for (int i = 0; i < 2; ++i)
		for (const char* q = tail[i]; *q && n < (int)sizeof g_prompt - 1; ++q)
			g_prompt[n++] = *q;
	g_prompt[n] = 0;
	g_prompt_n = n;

	sayn(g_prompt, n);
	flush_out();
}

static int read_line(char* out) {
	tty_set(g_tty, kTtySig | kTtyNonblock);

	g_n = 0;
	g_pos = 0;
	g_drawn = 0;
	g_line[0] = 0;
	g_browse = g_hist_count;
	g_interrupted = 0;

	for (;;) {
		pump_input();

		if (g_interrupted) {
			g_interrupted = 0;
			g_n = 0;
			g_pos = 0;
			g_drawn = 0;
			g_line[0] = 0;
			g_browse = g_hist_count;
			say("\r\n");
			flush_out();
			set_prompt();
			flush_out();
			continue;
		}

		if (!next_key()) {
			sleep_ms(20);
			continue;
		}
		const int k = g_key;

		if (k == kKeyEnter) {
			say("\r\n");
			flush_out();
			break;
		}
		if (k == kKeyEof) {
			if (g_n == 0) {
				tty_set(g_tty, kTtyCanon | kTtyEcho | kTtySig);
				return -1;
			}
			continue;
		}
		if (k == kKeyBackspace) {
			delete_back();
			continue;
		}
		if (k == kKeyDel) {
			delete_forward();
			continue;
		}
		if (k == kKeyLeft) {
			move_cursor(-1);
			continue;
		}
		if (k == kKeyRight) {
			move_cursor(1);
			continue;
		}
		if (k == kKeyHome) {
			g_pos = 0;
			place_cursor();
			flush_out();
			continue;
		}
		if (k == kKeyEnd) {
			g_pos = g_n;
			place_cursor();
			flush_out();
			continue;
		}
		if (k == kKeyUp) {
			hist_up();
			continue;
		}
		if (k == kKeyDown) {
			hist_down();
			continue;
		}
		if (k == kKeyTab) {
			tab_complete();
			continue;
		}
		if (k == kKeyNone)
			continue;
		if (k == kKeyChar && g_keych >= 0x20 && g_keych < 0x7f) {
			insert_char(g_keych);
			continue;
		}
	}

	hist_add();
	tty_set(g_tty, kTtyCanon | kTtyEcho | kTtySig);
	strcpy(out, g_line);
	return 0;
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	g_tty = open("/dev/tty", O_RDWR);
	if (g_tty < 0) {
		// without a terminal there is no shell
		printf("sh: no /dev/tty\n");
		return 1;
	}

	env_assign("PATH", "/bin");
	env_assign("HOME", "/");
	env_assign("TERM", "vt100");

	sigaction sa;
	sa.handler = (uint32_t)(uintptr_t)on_interrupt;
	sa.flags = 0;
	sa.mask = 0;
	sig_set(kSigInt, &sa, nullptr);

	tty_set_owner(g_tty, (uint32_t)getpid());

	for (;;) {
		for (;;) {
			int st = 0;
			const int got = waitpid(-1, &st, kWNohang);
			if (got <= 0)
				break;
			const int k = find_job(got);
			if (k >= 0) {
				g_jobs[k].running = false;
				g_jobs[k].status = st;
				sh_printf("\r\n[%d] %-8s %s\r\n", got, "done", g_jobs[k].name);
			}
			fg_drop(got);
			flush_out();
		}

		set_prompt();
		flush_out();

		char line[kLineMax + 1];
		if (read_line(line) < 0)
			break; // end of file at an empty line
		if (line[0] == 0)
			continue;

		// the line editor works on its own copy, so the parser may rewrite it
		char work[kLineMax + 1];
		strcpy(work, line);
		run_line(work);
	}

	sh_printf("\nsh: input ended\n");
	return 0;
}
