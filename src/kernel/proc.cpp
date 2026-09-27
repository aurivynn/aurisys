// /proc is virtual and gets made from the process table when read nothing is stored on disk so new/dead processes just
// show up
//
// /proc/<pid>/ has stuff like stat, status, cmdline, cwd, maps, fd/
//
// nodes dont have much extra space so kind + pid are stored in inode/size/internal, with the top inode bit set so it
// cant conflict with real ext4 inodes

#include "proc.h"

#include "lib/mem.h"
#include "lib/str.h"
#include "task.h"
#include "vfs.h"

#include <stdint.h>

namespace procfs {

namespace {

enum kind : uint32_t {
	kRoot = 1,
	kPidDir,
	kStat,
	kStatus,
	kCmdline,
	kCwd,
	kMaps,
	kFdDir,
	kFdLink,
};

constexpr uint32_t kVirtual = 0x80000000u;
constexpr uint32_t kFiles = 6;

struct tag {
	kind k() const { return (kind)n.pad2; }
	uint32_t pid() const { return n.inode & ~kVirtual; }
	uint32_t fd() const { return (uint32_t)(uintptr_t)n.internal; }
	vfs::node n;
};

// the directories are fixed, one per process, so they are static and always the same node for the same pid.
vfs::node g_root;
tag g_pids[task::kMaxTask];
tag g_fddirs[task::kMaxTask];
constexpr uint32_t kRing = 8;
tag g_ring[kRing];
uint32_t g_ring_at = 0;

const struct {
	const char* name;
	kind k;
} kEntries[kFiles] = {
	{"cmdline", kCmdline}, {"cwd", kCwd}, {"fd", kFdDir}, {"maps", kMaps}, {"stat", kStat}, {"status", kStatus},
};

task::task* by_pid(uint32_t pid) {
	task::task* t = task::by_pid(pid);
	return (t && t->state != task::kFree) ? t : nullptr;
}

task::task* nth(uint32_t index) {
	uint32_t seen = 0;
	for (int i = 0; i < task::kMaxTask; ++i) {
		task::task* t = &task::g_tasks[i];
		if (t->state == task::kFree)
			continue;
		if (seen == index)
			return t;
		++seen;
	}
	return nullptr;
}

// a pid or an fd number as text
void num_to_str(char* out, uint32_t v) {
	char d[16];
	uint32_t r = 0;
	do {
		if (r < 16)
			d[r] = (char)('0' + v % 10u);
		++r;
		v /= 10u;
	} while (v);
	uint32_t i = 0;
	while (r && i < 15)
		out[i++] = d[--r];
	out[i] = 0;
}

int parse(const char* s) {
	if (!s || *s < '0' || *s > '9')
		return -1;
	int v = 0;
	while (*s >= '0' && *s <= '9') {
		v = v * 10 + (*s - '0');
		if (v > 100000)
			return -1;
		++s;
	}
	return *s ? -1 : v;
}

struct sink {
	char* buf;
	uint32_t len;
	uint32_t n;
	uint32_t off;
};

__attribute__((noinline)) void put(sink& s, const char* t) {
	for (uint32_t i = 0; t[i] && s.n < s.len; ++i)
		s.buf[s.n++] = t[i];
}

__attribute__((noinline)) void num(sink& s, uint32_t v) {
	char d[16];
	uint32_t r = 0;
	do {
		d[r++] = (char)('0' + v % 10u);
		v /= 10u;
	} while (v);
	while (r && s.n < s.len)
		s.buf[s.n++] = d[--r];
}

void field(sink& s, uint32_t v) {
	num(s, v);
	put(s, " ");
}

__attribute__((noinline)) int finish(sink& s) {
	if (s.off >= s.n)
		return 0;
	const uint32_t left = s.n - s.off;
	const uint32_t give = s.len < left ? s.len : left;
	memmove(s.buf, s.buf + s.off, give);
	return (int)give;
}

uint32_t open_fds(const task::task* t) {
	uint32_t n = 0;
	for (int i = 0; i < vfs::kMaxFd; ++i)
		if (t->fd[i].n.type)
			++n;
	return n;
}

__attribute__((noinline)) void fill_stat(const tag& g, sink& s) {
	task::task* t = by_pid(g.pid());
	if (!t)
		return;

	field(s, t->pid);
	put(s, "(");
	put(s, t->name);
	put(s, ") ");
	put(s, task::state_name(t->state));
	put(s, " ");
	field(s, t->ppid);
	field(s, t->utime);
	field(s, t->ktime);
	put(s, task::signal_name(t->signal));
	put(s, "\n");
}

__attribute__((noinline)) void fill_status(const tag& g, sink& s) {
	task::task* t = by_pid(g.pid());
	if (!t)
		return;
	put(s, "Name:\t");
	put(s, t->name);
	put(s, "\nState:\t");
	put(s, task::state_name(t->state));
	put(s, "\nPid:\t");
	num(s, t->pid);
	put(s, "\nPPid:\t");
	num(s, t->ppid);
	put(s, "\nSignal:\t");
	num(s, t->signal);
	put(s, " ");
	put(s, task::signal_name(t->signal));
	put(s, "\nUtime:\t");
	num(s, t->utime);
	put(s, "\nKtime:\t");
	num(s, t->ktime);
	put(s, "\nExit:\t");
	num(s, t->exit_code);
	put(s, "\nFds:\t");
	num(s, open_fds(t));
	put(s, "\n");
}

__attribute__((noinline)) void fill_cmdline(const tag& g, sink& s) {
	task::task* t = by_pid(g.pid());
	if (!t)
		return;
	put(s, t->name);

	if (s.n < s.len)
		s.buf[s.n++] = 0;
}

__attribute__((noinline)) void fill_cwd(const tag& g, sink& s) {
	task::task* t = by_pid(g.pid());
	if (!t)
		return;

	put(s, t->cwd);
	put(s, "\n");
}

__attribute__((noinline)) void fill_maps(const tag&, sink& s) {
	put(s, "40000000-4003ffff r-xp 00000000 00:00 0                  code\n");
	put(s, "40100000-4010ffff rw-p 00000000 00:00 0                  stack\n");
	put(s, "40200000-402fffff rw-p 00000000 00:00 0                  arena\n");
}

__attribute__((noinline)) void fill_fd(const tag& g, sink& s) {
	task::task* t = by_pid(g.pid());
	if (!t || g.fd() >= vfs::kMaxFd || t->fd[g.fd()].n.type == 0) {
		put(s, "gone\n");
		return;
	}
	num(s, g.fd());
	put(s, " -> ");
	put(s, t->fd[g.fd()].n.name);
	put(s, "\n");
}

int read_file(vfs::node* n, void* buf, uint32_t off, uint32_t len) {
	if (!n || !buf || len == 0)
		return 0;
	const tag& g = *reinterpret_cast<tag*>(n);
	sink s = {(char*)buf, len, 0, off};
	switch (g.k()) {
	case kStat:
		fill_stat(g, s);
		break;
	case kStatus:
		fill_status(g, s);
		break;
	case kCmdline:
		fill_cmdline(g, s);
		break;
	case kCwd:
		fill_cwd(g, s);
		break;
	case kMaps:
		fill_maps(g, s);
		break;
	case kFdLink:
		fill_fd(g, s);
		break;
	default:
		return 0;
	}
	return finish(s);
}

// building nodes

void dress(tag& g, vfs::node* parent, const char* name, kind k, uint32_t pid, uint32_t fd) {
	vfs::node* n = &g.n;
	memset(n, 0, sizeof *n);
	n->name = n->name_buf;
	strncpy(n->name_buf, name, sizeof n->name_buf - 1);
	n->name_buf[sizeof n->name_buf - 1] = 0;
	n->inode = pid | kVirtual;
	n->size = 0; // a computed file has no length until it is read
	n->pad2 = (uint16_t)k;
	n->parent = parent;
	n->internal = (void*)(uintptr_t)fd;
	n->readdir = nullptr;
	n->find_child = nullptr;
	n->read = nullptr;
	n->write = nullptr;
	const bool dir = (k == kPidDir || k == kFdDir);
	n->type = dir ? vfs::kTypeDir : vfs::kTypeFile;
	if (!dir)
		n->read = read_file;
}

tag* leaf(vfs::node* parent, const char* name, kind k, uint32_t pid, uint32_t fd) {
	tag* g = &g_ring[g_ring_at];
	g_ring_at = (g_ring_at + 1) % kRing;
	dress(*g, parent, name, k, pid, fd);
	return g;
}

// the directories refer to each other so they are all named up front
int readdir_pid(vfs::node* n, uint32_t index, vfs::node* out);
int readdir_fd(vfs::node* n, uint32_t index, vfs::node* out);
vfs::node* find_pid(vfs::node* n, const char* name);
vfs::node* find_fd(vfs::node* n, const char* name);

void attach_dir(tag& g, const char* name, uint32_t pid) {
	dress(g, &g_root, name, kPidDir, pid, 0);
	g.n.readdir = readdir_pid;
	g.n.find_child = find_pid;
}

void attach_fddir(tag& g, uint32_t pid) {
	dress(g, &g_pids[pid - 1].n, "fd", kFdDir, pid, 0);
	g.n.readdir = readdir_fd;
	g.n.find_child = find_fd;
}

int readdir_proc(vfs::node*, uint32_t index, vfs::node* out) {
	task::task* t = nth(index);
	if (!t)
		return -1;
	char nm[12];
	num_to_str(nm, t->pid);

	attach_dir(g_pids[t->pid - 1], nm, t->pid);
	attach_fddir(g_fddirs[t->pid - 1], t->pid);
	*out = g_pids[t->pid - 1].n;
	out->name = out->name_buf;
	out->parent = &g_root;
	return 0;
}

vfs::node* find_proc(vfs::node*, const char* name) {
	const int pid = parse(name);
	if (pid < 1 || pid > task::kMaxTask || !by_pid((uint32_t)pid))
		return nullptr;
	attach_dir(g_pids[pid - 1], name, (uint32_t)pid);
	attach_fddir(g_fddirs[pid - 1], (uint32_t)pid);
	return &g_pids[pid - 1].n;
}

int readdir_pid(vfs::node* n, uint32_t index, vfs::node* out) {
	if (index >= kFiles)
		return -1;
	const uint32_t pid = reinterpret_cast<tag*>(n)->pid();
	tag* g = leaf(n, kEntries[index].name, kEntries[index].k, pid, 0);
	if (!g)
		return -1;
	*out = g->n;
	out->name = out->name_buf;
	out->parent = n;
	if (kEntries[index].k == kFdDir) {
		out->readdir = readdir_fd;
		out->find_child = find_fd;
	}
	return 0;
}

vfs::node* find_pid(vfs::node* n, const char* name) {
	const uint32_t pid = reinterpret_cast<tag*>(n)->pid();
	for (uint32_t i = 0; i < kFiles; ++i) {
		if (strcmp(name, kEntries[i].name) != 0)
			continue;
		tag* g = leaf(n, name, kEntries[i].k, pid, 0);
		if (g && kEntries[i].k == kFdDir) {
			g->n.readdir = readdir_fd;
			g->n.find_child = find_fd;
		}
		return &g->n;
	}
	return nullptr;
}

int readdir_fd(vfs::node* n, uint32_t index, vfs::node* out) {
	const tag& self = *reinterpret_cast<tag*>(n);
	task::task* t = by_pid(self.pid());
	if (!t)
		return -1;
	uint32_t seen = 0;
	for (uint32_t i = 0; i < vfs::kMaxFd; ++i) {
		if (t->fd[i].n.type == 0)
			continue;
		if (seen != index) {
			++seen;
			continue;
		}
		char nm[8];
		num_to_str(nm, i);
		*out = leaf(n, nm, kFdLink, self.pid(), i)->n;
		out->name = out->name_buf;
		out->parent = n;
		return 0;
	}
	return -1;
}

vfs::node* find_fd(vfs::node* n, const char* name) {
	const tag& self = *reinterpret_cast<tag*>(n);
	const int fd = parse(name);
	task::task* t = by_pid(self.pid());
	if (fd < 0 || !t || fd >= vfs::kMaxFd || t->fd[fd].n.type == 0)
		return nullptr;
	return &leaf(n, name, kFdLink, self.pid(), (uint32_t)fd)->n;
}

} // namespace

void init() {
	g_ring_at = 0;
	memset(&g_root, 0, sizeof g_root);
	g_root.name = g_root.name_buf;
	strcpy(g_root.name_buf, "proc");
	g_root.type = vfs::kTypeDir;
	g_root.readdir = readdir_proc;
	g_root.find_child = find_proc;
	g_root.inode = kVirtual;
	vfs::mount(&g_root, "/proc");
}

} // namespace procfs
