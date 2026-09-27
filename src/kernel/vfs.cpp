// the virtual filesystem. nodes with ops over the ext4 backend, devfs
// hung at /dev, and the fd table that terminal::printf goes through

#include "vfs.h"

#include "drivers/console.h"
#include "drivers/kbd.h"
#include "drivers/serial.h"
#include "fs.h"
#include "lib/mem.h"
#include "lib/str.h"
#include "task.h"

#include <stdint.h>

namespace vfs {

namespace {

// pool of transient nodes. the root, devfs and the devices are separate statics so they never get recycled. resolve
// hands out pool nodes and callers use them right away, fine while we are single threaded
constexpr int kPool = 40;
node g_pool[kPool];
int g_head = 0;

node* pool_fresh() {
	const int s = g_head;
	g_head = (g_head + 1) % kPool;
	memset(&g_pool[s], 0, sizeof g_pool[s]);
	return &g_pool[s];
}

// mounts keyed by inode, not by node pointer: the pool recycles slots so a mount held on a single node would go stale.
// resolve reattaches the tree when a path walks through the matching inode
struct mnt {
	uint32_t ino;
	node* tree;
};
mnt g_mounts[8];
int g_mounts_n = 0;

node g_root;  // the ext4 root
node g_devfs; // the dev tree root

// ext4 backend adapter

int ext_read(node* n, void* buf, uint32_t off, uint32_t len) { return (int)fs::read(n->inode, buf, len, off); }

node* ext_find_child(node* n, const char* name);

int ext_readdir(node* n, uint32_t index, node* out) {
	struct hit {
		uint32_t want, seen;
		bool found;
		uint32_t ino;
		uint8_t type;
		char name[64];
	};
	hit c = {index, 0, false, 0, 0, {}};
	auto cb = [](const char* nm, uint32_t ino, uint8_t t, void* p) -> bool {
		hit* h = (hit*)p;
		if (h->seen != h->want)
			++h->seen;
		if (h->seen != h->want)
			return true;
		h->found = true;
		h->ino = ino;
		h->type = t;
		int l = 0;
		while (nm[l] && l < 63) {
			h->name[l] = nm[l];
			++l;
		}
		h->name[l] = 0;
		return false;
	};
	fs::list_dir(n->inode, cb, &c);
	if (!c.found)
		return -1;
	memset(out, 0, sizeof *out);
	int l = 0;
	while (c.name[l] && l < (int)sizeof out->name_buf - 1) {
		out->name_buf[l] = c.name[l];
		++l;
	}
	out->name_buf[l] = 0;
	out->name = out->name_buf;
	out->inode = c.ino;
	out->type = c.type;
	out->parent = n;
	out->read = ext_read;
	out->readdir = ext_readdir;
	out->find_child = ext_find_child;
	fs::stat st;
	if (fs::getstat(c.ino, &st))
		out->size = st.size;
	return 0;
}

node* ext_find_child(node* n, const char* name) {
	struct hit {
		const char* want;
		bool found;
		uint32_t ino;
		uint8_t type;
	};
	hit c = {name, false, 0, 0};
	auto cb = [](const char* nm, uint32_t ino, uint8_t t, void* p) -> bool {
		hit* h = (hit*)p;
		if (strcmp(nm, h->want) != 0)
			return true;
		h->ino = ino;
		h->type = t;
		h->found = true;
		return false;
	};
	fs::list_dir(n->inode, cb, &c);
	if (!c.found)
		return nullptr;
	node* out = pool_fresh();
	if (!out)
		return nullptr;
	out->name = out->name_buf;
	strncpy(out->name_buf, name, sizeof out->name_buf - 1);
	out->inode = c.ino;
	out->type = c.type;
	out->parent = n;
	out->read = ext_read;
	out->readdir = ext_readdir;
	out->find_child = ext_find_child;
	fs::stat st;
	if (fs::getstat(c.ino, &st))
		out->size = st.size;
	return out;
}

// devfs

typedef int (*dev_rd)(void* in, void* buf, uint32_t len);
typedef int (*dev_wr)(void* in, const void* buf, uint32_t len);

struct dev {
	const char* name;
	dev_rd rd;
	dev_wr wr;
};

int dev_null_rd(void*, void*, uint32_t len) {
	(void)len;
	return 0; // nothing here, reads hit eof
}
int dev_null_wr(void*, const void*, uint32_t len) { return (int)len; } // swallows it all

int dev_zero_rd(void*, void* buf, uint32_t len) {
	memset(buf, 0, len);
	return (int)len;
}
int dev_zero_wr(void*, const void*, uint32_t len) { return (int)len; }

int dev_console_rd(void*, void* buf, uint32_t len) {
	if (len == 0)
		return 0;
	int c = kbd::poll();
	if (c < 0)
		c = serial::recv();
	if (c < 0)
		return 0; // nothing waiting, non block for now
	((uint8_t*)buf)[0] = (uint8_t)c;
	return 1;
}
int dev_console_wr(void*, const void* buf, uint32_t len) {
	const char* s = (const char*)buf;
	for (uint32_t i = 0; i < len; ++i) {
		serial::putc(s[i]);
		console::putchar(s[i]);
	}
	return (int)len;
}

int dev_serial_rd(void*, void* buf, uint32_t len) {
	if (len == 0)
		return 0;
	int c = serial::recv();
	if (c < 0)
		return 0;
	((uint8_t*)buf)[0] = (uint8_t)c;
	return 1;
}
int dev_serial_wr(void*, const void* buf, uint32_t len) {
	const char* s = (const char*)buf;
	for (uint32_t i = 0; i < len; ++i)
		serial::putc(s[i]);
	return (int)len;
}

int dev_kbd_rd(void*, void* buf, uint32_t len) {
	if (len == 0)
		return 0;
	int c = kbd::poll();
	if (c < 0)
		return 0;
	((uint8_t*)buf)[0] = (uint8_t)c;
	return 1;
}

int dev_fb_wr(void*, const void* buf, uint32_t len) {
	const char* s = (const char*)buf;
	for (uint32_t i = 0; i < len; ++i)
		console::putchar(s[i]); // to the screen only, no serial
	return (int)len;
}

const dev g_devs[] = {
	{"null", dev_null_rd, dev_null_wr},
	{"zero", dev_zero_rd, dev_zero_wr},
	{"console", dev_console_rd, dev_console_wr},
	{"serial", dev_serial_rd, dev_serial_wr},
	{"kbd", dev_kbd_rd, nullptr},
	{"fb", nullptr, dev_fb_wr},
};
constexpr uint32_t kDevCount = sizeof(g_devs) / sizeof(g_devs[0]);

int dev_read(node* n, void* buf, uint32_t off, uint32_t len) {
	(void)off;
	dev* d = (dev*)n->internal;
	return d->rd ? d->rd(d, buf, len) : 0;
}
int dev_write(node* n, const void* buf, uint32_t off, uint32_t len) {
	(void)off;
	dev* d = (dev*)n->internal;
	return d->wr ? d->wr(d, buf, len) : 0;
}

int dev_readdir(node* n, uint32_t index, node* out) {
	(void)n;
	dev* d = (dev*)n->internal;
	if (index >= kDevCount)
		return -1;
	memset(out, 0, sizeof *out);
	int l = 0;
	while (d[index].name[l] && l < (int)sizeof out->name_buf - 1) {
		out->name_buf[l] = d[index].name[l];
		++l;
	}
	out->name_buf[l] = 0;
	out->name = out->name_buf;
	out->type = kTypeChar;
	out->parent = n;
	out->read = dev_read;
	out->write = dev_write;
	out->internal = (void*)&d[index];
	return 0;
}

node* dev_find_child(node* n, const char* name) {
	dev* d = (dev*)n->internal;
	for (uint32_t i = 0; i < kDevCount; ++i) {
		if (strcmp(name, d[i].name) != 0)
			continue;
		node* out = pool_fresh();
		if (!out)
			return nullptr;
		out->name = out->name_buf;
		strncpy(out->name_buf, name, sizeof out->name_buf - 1);
		out->type = kTypeChar;
		out->parent = n;
		out->read = dev_read;
		out->write = dev_write;
		out->internal = (void*)&d[i];
		return out;
	}
	return nullptr;
}

} // namespace

// walk a path a component at a time. fs already resolves relative to
// cwd and folds .. so only plain names reach the find_child calls
node* resolve(const char* path) {
	if (!path)
		return nullptr;
	char abs[256];
	if (!fs::resolve(path, task::cwd(), abs, sizeof abs))
		return nullptr;
	node* cur = &g_root;
	char* tok = abs;
	for (;;) {
		while (*tok == '/')
			++tok;
		if (!*tok)
			break;
		char* end = tok;
		while (*end && *end != '/')
			++end;
		const char saved = *end;
		*end = 0;
		node* under = cur->mount ? cur->mount : cur;
		node* child = under->find_child ? under->find_child(under, tok) : nullptr;
		*end = saved;
		if (!child)
			return nullptr;
		// a mount point gets its tree reattached on the way through
		for (int i = 0; i < g_mounts_n; ++i)
			if (child->inode && g_mounts[i].ino == child->inode)
				child->mount = g_mounts[i].tree;
		cur = child;
		if (*end == 0)
			break; // hit the path end, no more components
		tok = end + 1;
	}
	return cur;
}

node* root() { return &g_root; }

int readdir(node* n, uint32_t index, node* out) {
	if (!n)
		return -1;
	// a mount point reads as its mounted tree, its own dirents are hidden
	node* t = n->mount ? n->mount : n;
	if (!t->readdir)
		return -1;
	return t->readdir(t, index, out);
}

// hangs a tree on a dir, making the dir first if it does not exist yet
bool mount(node* tree, const char* at) {
	if (!tree)
		return false;
	if (g_mounts_n >= 8)
		return false;
	node* mp = resolve(at);
	if (!mp) {
		if (!fs::mkdir("/", at))
			return false;
		mp = resolve(at);
		if (!mp)
			return false;
	}
	if (mp->type != kTypeDir)
		return false;
	g_mounts[g_mounts_n].ino = mp->inode;
	g_mounts[g_mounts_n].tree = tree;
	++g_mounts_n;
	tree->parent = &g_root; // .. from inside the tree lands at root
	return true;
}

// the fd table. it lives in the process now, so two processes can have fd 3 open on different files at the same time
static ofile* fds() { return task::g_current ? task::g_current->fd : nullptr; }

int fd_open(const char* path, uint32_t flags) {
	ofile* g_fd = fds();
	if (!g_fd)
		return -1;
	if (!path)
		return -1;
	node* r = resolve(path);
	if (!r)
		return -1;
	if (r->inode && r->type == kTypeFile && (flags & O_TRUNC)) {
		if (!fs::write_file(task::cwd(), path, nullptr, 0, O_TRUNC))
			return -1;
		r->size = 0; // keep the node honest about the truncate
	}
	int fd = -1;
	for (int i = 0; i < kMaxFd; ++i)
		if (g_fd[i].n.type == 0) {
			fd = i;
			break;
		}
	if (fd < 0)
		return -1;
	ofile* e = &g_fd[fd];
	memcpy(&e->n, r, sizeof e->n);
	e->n.name = e->n.name_buf; // the copy carries its own name buffer
	e->pos = 0;
	e->flags = flags;
	if ((flags & O_APPEND) && e->n.inode) {
		fs::stat st;
		if (fs::getstat(e->n.inode, &st))
			e->pos = st.size; // append starts at the end
	}
	return fd;
}

int fd_close(int fd) {
	ofile* g_fd = fds();
	if (!g_fd)
		return -1;
	if (fd < 0 || fd >= kMaxFd)
		return -1;
	memset(&g_fd[fd], 0, sizeof g_fd[fd]);
	return 0;
}

int fd_read(int fd, void* buf, uint32_t len) {
	ofile* g_fd = fds();
	if (!g_fd)
		return -1;
	if (fd < 0 || fd >= kMaxFd)
		return -1;
	ofile* e = &g_fd[fd];
	if (!e->n.read)
		return -1;
	const int r = e->n.read(&e->n, buf, e->pos, len);
	if (r > 0)
		e->pos += (uint32_t)r;
	return r;
}

int fd_write(int fd, const void* buf, uint32_t len) {
	ofile* g_fd = fds();
	if (!g_fd)
		return -1;
	if (fd < 0 || fd >= kMaxFd)
		return -1;
	ofile* e = &g_fd[fd];
	if (!e->n.write)
		return -1;
	// append means every write lands at the end, like O_APPEND
	if ((e->flags & O_APPEND) && e->n.inode) {
		fs::stat st;
		if (fs::getstat(e->n.inode, &st))
			e->pos = st.size;
	}
	const int r = e->n.write(&e->n, buf, e->pos, len);
	if (r > 0)
		e->pos += (uint32_t)r;
	return r;
}

// whence 0 set, 1 cur, 2 end, returns the new offset
int lseek(int fd, int off, int whence) {
	ofile* g_fd = fds();
	if (!g_fd)
		return -1;
	if (fd < 0 || fd >= kMaxFd)
		return -1;
	ofile* e = &g_fd[fd];
	if (!e->n.type)
		return -1;
	uint32_t base = 0;
	if (whence == 1)
		base = e->pos;
	else if (whence == 2)
		base = e->n.size;
	else if (whence != 0)
		return -1;
	if (off < 0 && (uint32_t)(-off) > base)
		return -1;
	e->pos = base + (uint32_t)off;
	return (int)e->pos;
}

// point one fd at the same open file, vacating the target first
int dup2(int old, int nw) {
	ofile* g_fd = fds();
	if (!g_fd)
		return -1;
	if (old < 0 || old >= kMaxFd || nw < 0 || nw >= kMaxFd)
		return -1;
	if (old == nw)
		return nw;
	if (g_fd[nw].n.type)
		fd_close(nw);
	memcpy(&g_fd[nw], &g_fd[old], sizeof g_fd[nw]);
	g_fd[nw].n.name = g_fd[nw].n.name_buf;
	return nw;
}

node* fd_node(int fd) {
	ofile* g_fd = fds();
	if (!g_fd)
		return nullptr;
	if (fd < 0 || fd >= kMaxFd)
		return nullptr;
	return &g_fd[fd].n;
}

// PATH

const char kPath[] = "/bin";

const char* path() { return kPath; }

// first PATH entry that holds the name wins
bool find_in_path(const char* name, char* out, uint32_t outsz) {
	if (!name || !out || outsz < 2)
		return false;
	const char* p = kPath;
	while (*p) {
		char dir[128];
		uint32_t dn = 0;
		while (*p && *p != ':' && dn < sizeof dir - 1)
			dir[dn++] = *p++;
		dir[dn] = 0;
		if (*p == ':')
			++p;
		if (dn == 0)
			continue;
		char full[256];
		uint32_t f = 0;
		for (uint32_t i = 0; dir[i] && f < sizeof full - 2; ++i)
			full[f++] = dir[i];
		if (full[f - 1] != '/')
			full[f++] = '/';
		for (const char* q = name; *q && f < sizeof full - 1; ++q)
			full[f++] = *q;
		full[f] = 0;
		if (!resolve(full))
			continue;
		const uint32_t c = f < outsz - 1 ? f : outsz - 1;
		memcpy(out, full, c);
		out[c] = 0;
		return true;
	}
	return false;
}

// boot

bool init() {
	memset(&g_root, 0, sizeof g_root);
	g_root.name = g_root.name_buf;
	strcpy(g_root.name_buf, "/");
	g_root.inode = 2;
	g_root.type = kTypeDir;
	g_root.readdir = ext_readdir;
	g_root.find_child = ext_find_child;
	fs::stat st;
	if (!fs::getstat(2, &st))
		return false;
	g_root.size = st.size;

	memset(&g_devfs, 0, sizeof g_devfs);
	g_devfs.name = g_devfs.name_buf;
	strcpy(g_devfs.name_buf, "dev");
	g_devfs.type = kTypeDir;
	g_devfs.readdir = dev_readdir;
	g_devfs.find_child = dev_find_child;
	g_devfs.internal = (void*)g_devs;

	if (!mount(&g_devfs, "/dev"))
		return false;

	const dev* cons = nullptr;
	for (uint32_t i = 0; i < kDevCount; ++i)
		if (strcmp(g_devs[i].name, "console") == 0)
			cons = &g_devs[i];
	if (!cons)
		return false;
	ofile* g_fd = fds();
	if (!g_fd)
		return false;
	memset(g_fd, 0, sizeof g_fd[0] * 3);
	for (int i = 0; i < 3; ++i) {
		ofile* e = &g_fd[i];
		e->n.name = e->n.name_buf;
		strcpy(e->n.name_buf, "console");
		e->n.type = kTypeChar;
		e->n.read = dev_read;
		e->n.write = dev_write;
		e->n.internal = (void*)cons;
		// pos and flags stay zero
	}
	return true;
}

} // namespace vfs