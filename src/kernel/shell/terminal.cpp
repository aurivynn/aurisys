// the terminal

#include "shell/terminal.h"

#include "apps/app.h"
#include "drivers/console.h"
#include "drivers/fb.h"
#include "drivers/kbd.h"
#include "drivers/serial.h"
#include "fs.h"
#include "lib/mem.h"
#include "lib/print.h"
#include "lib/str.h"
#include "lib/time.h"
#include "vfs.h"

#include <stdarg.h>
#include <stdint.h>

namespace terminal {

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

// wait for a key (keyboard first, then serial), blinking while idle
int read_char() {
	for (;;) {
		cursor_tick();
		int c = kbd::poll();
		if (c >= 0)
			return c;
		c = serial::recv();
		if (c >= 0)
			return c;
	}
}

// split a line into argv (in place). returns argc, 0 = empty line
int tokenize(char* line, const char** argv, int argv_max) {
	int argc = 0;
	char* p = line;
	for (;;) {
		while (*p == ' ')
			++p;
		if (!*p)
			break;
		if (argc >= argv_max)
			break;
		argv[argc++] = p;
		while (*p && *p != ' ')
			++p;
		if (*p)
			*p++ = 0;
	}
	return argc;
}

void run_line(char* line) {
	const char* argv[8];
	const int argc = tokenize(line, argv, 8);
	if (argc == 0)
		return;
	if (apps::run(argv[0], argc, argv) < 0) {
		char pp[256];
		if (vfs::find_in_path(argv[0], pp, sizeof pp))
			terminal::printf("%s: found at %s, no disk exec yet (phase e)\n", argv[0], pp);
		else
			terminal::printf("unknown command: %s\n", argv[0]);
	}
}

void prompt() {
	console::setcolor(0xCBA6F7, 0x1E1E2E);
	terminal::printf("aurisys ");
	console::setcolor(0xA6E3A1, 0x1E1E2E);
	terminal::printf("%s", fs::cwd());
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
	for (int i = 0; i < apps::count(); ++i)
		consider(apps::table()[i].name);

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
		if (!fs::resolve(tmp, dir, sizeof dir))
			return;
	} else {
		strncpy(dir, fs::cwd(), sizeof dir - 1);
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
	for (;;) {
		prompt();
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