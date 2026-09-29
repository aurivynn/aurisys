// the virtual filesystem. nodes with ops over the ext4 backend, devfs
// hung at /dev, and the fd table that terminal::printf goes through

#include "vfs.h"

#include "drivers/console.h"
#include "drivers/kbd.h"
#include "drivers/serial.h"
#include "fs.h"
#include "lib/mem.h"
#include "lib/str.h"
#include "shell/terminal.h"
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

// writing through a descriptor
int ext_write(node* n, const void* buf, uint32_t off, uint32_t len) {
	if (!n->inode)
		return -1;
	if (!fs::write_at(n->inode, buf, off, len))
		return -1;
	fs::stat st;
	if (fs::getstat(n->inode, &st))
		n->size = st.size;
	return (int)len;
}

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
	out->write = ext_write;
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
	out->write = ext_write;
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
typedef int (*dev_ioctl_fn)(void* in, uint32_t req, void* arg);

struct dev {
	const char* name;
	dev_rd rd;
	dev_wr wr;
	dev_ioctl_fn ctl;
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

int tty_take(void* buf, uint32_t len);

int dev_console_rd(void*, void* buf, uint32_t len) {
	if (len == 0)
		return 0;
	return tty_take(buf, len);
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
	for (;;) {
		const int c = kbd::poll();
		if (c >= 0) {
			((uint8_t*)buf)[0] = (uint8_t)c;
			return 1;
		}
		kbd::wait();
	}
}

int dev_fb_wr(void*, const void* buf, uint32_t len) {
	const char* s = (const char*)buf;
	for (uint32_t i = 0; i < len; ++i)
		console::putchar(s[i]); // to the screen only, no serial
	return (int)len;
}

constexpr uint32_t kTtyBuf = 256;

struct tty {
	char buf[kTtyBuf];
	uint32_t head; // where the next byte goes
	uint32_t tail; // where the next byte is taken from
	uint32_t flags;

	// the process an interrupt is sent to
	uint32_t owner;

	// set when the discipline has seen an end of file and nobody has taken it yet
	bool eof;
};

// set by ioctl when a reader would rather be told there is nothing than be made to wait for it.
bool g_tty_nonblock;

tty g_tty;

void tty_echo(const char* s, uint32_t n) {
	for (uint32_t i = 0; i < n; ++i) {
		serial::putc(s[i]);
		console::putchar(s[i]);
	}
}
void tty_echo(char c) { tty_echo(&c, 1); }

int tty_buffered() { return g_tty.head != g_tty.tail ? 1 : 0; }

uint32_t tty_count() { return (g_tty.head + kTtyBuf - g_tty.tail) % kTtyBuf; }

void tty_put(char c) {
	if (tty_count() + 1 >= kTtyBuf)
		return;
	g_tty.buf[g_tty.head] = c;
	g_tty.head = (g_tty.head + 1u) % kTtyBuf;
}

void tty_drop() { g_tty.head = g_tty.tail; }

int tty_key(bool block) {
	int c = kbd::poll_char();
	if (c >= 0)
		return c;
	const int s = (int)serial::recv();
	if (s >= 0)
		return s;
	if (block)
		kbd::wait(); // parks until a key arrives and serial wakes it on a tick
	return -1;
}

void tty_input(int c) {
	const bool canon = (g_tty.flags & kTtyCanon) != 0;
	const bool echo = (g_tty.flags & kTtyEcho) != 0;
	const bool sigs = (g_tty.flags & kTtySig) != 0;

	if (c == 0x03 && sigs) {
		tty_drop();
		if (echo)
			tty_echo("^C\r\n", 4);
		const uint32_t to = g_tty.owner ? g_tty.owner : (task::g_current ? task::g_current->pid : 0u);
		if (to)
			task::signal_task(to, task::kSigInt);
		return;
	}

	if (c == 0x04 && canon) {
		if (!tty_buffered()) {
			g_tty.eof = true;
			return;
		}
		tty_put((char)c);
		if (echo)
			tty_echo((char)c);
		return;
	}

	if (c == 0x7F && canon) {
		if (g_tty.head != g_tty.tail) {
			g_tty.head = (g_tty.head - 1u + kTtyBuf) % kTtyBuf;
			if (echo)
				tty_echo("\b \b", 3);
		}
		return;
	}

	if (c == '\r' && canon)
		c = '\n';

	tty_put((char)c);
	if (echo) {
		if (c == '\n')
			tty_echo("\r\n", 2);
		else if (c == '\t')
			tty_echo("\t", 1);
		else if (c < 0x20)
			tty_echo((char)7);
		else
			tty_echo((char)c);
	}
}

int tty_take(void* buf, uint32_t len) {
	uint8_t* out = (uint8_t*)buf;
	uint32_t got = 0;

	while (got < len && !g_tty.eof && tty_buffered()) {
		out[got++] = (uint8_t)g_tty.buf[g_tty.tail];
		g_tty.tail = (g_tty.tail + 1u) % kTtyBuf;
	}
	return (int)got;
}

// deliver a whole line assembled here out of the keystrokes that make it up
int dev_tty_rd(void*, void* buf, uint32_t len) {
	if (len == 0)
		return 0;
	const bool canon = (g_tty.flags & kTtyCanon) != 0;
	uint8_t* out = (uint8_t*)buf;
	uint32_t got = 0;

	for (;;) {
		got += (uint32_t)tty_take(out + got, len - got);

		if (got) {
			if (!canon)
				return (int)got;
			// canonical mode hands over a line at a time
			for (uint32_t i = 0; i < got; ++i) {
				if (out[i] == '\n')
					return (int)i + 1;
			}
			if (got >= len)
				return (int)got;
		}

		if (g_tty.eof) {
			g_tty.eof = false;
			return (int)got;
		}

		if (g_tty_nonblock)
			return (int)got;

		tty_drain();
		if (tty_buffered() || g_tty.eof)
			continue;
		tty_key(true); // park until a keystroke arrives
	}
}

int dev_tty_wr(void*, const void* buf, uint32_t len) {
	tty_echo((const char*)buf, len);
	return (int)len;
}

int dev_tty_ctl(void*, uint32_t req, void* arg) {
	if (!arg)
		return -kErrInval;
	switch (req) {
	case kIoctlGetFlags:
		*(uint32_t*)arg = g_tty.flags | (g_tty_nonblock ? (uint32_t)kTtyNonblock : 0u);
		return 0;
	case kIoctlSetFlags:
		g_tty_nonblock = (*(uint32_t*)arg & kTtyNonblock) != 0;
		g_tty.flags = *(uint32_t*)arg & ~(uint32_t)kTtyNonblock;
		tty_drop(); // a line half typed under the old modes is not a line now
		return 0;
	case kIoctlGetOwner:
		*(uint32_t*)arg = g_tty.owner;
		return 0;
	case kIoctlSetOwner:
		g_tty.owner = *(uint32_t*)arg;
		return 0;
	default:
		return -kErrInval;
	}
}

const dev g_devs[] = {
	{"null", dev_null_rd, dev_null_wr, nullptr},
	{"zero", dev_zero_rd, dev_zero_wr, nullptr},
	{"console", dev_console_rd, dev_console_wr, nullptr},
	{"serial", dev_serial_rd, dev_serial_wr, nullptr},
	{"kbd", dev_kbd_rd, nullptr, nullptr},
	{"fb", nullptr, dev_fb_wr, nullptr},
	{"tty", dev_tty_rd, dev_tty_wr, dev_tty_ctl},
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
int dev_node_ctl(node* n, uint32_t req, void* arg) {
	dev* d = (dev*)n->internal;
	return d->ctl ? d->ctl(d, req, arg) : -kErrInval;
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
	out->ctl = dev_node_ctl;
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
		out->ctl = dev_node_ctl;
		out->internal = (void*)&d[i];
		return out;
	}
	return nullptr;
}

} // namespace

// take everything waiting at the keyboard or on the line and let the discipline have it
void tty_drain() {
	bool got = false;
	for (;;) {
		const int c = tty_key(false);
		if (c < 0)
			break;
		tty_input(c);
		got = true;
	}
	if (got)
		kbd::wake();
}

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

// the pipe storage
namespace {

struct pipebuf {
	uint8_t buf[kPipeBytes];
	uint32_t head; // where the next byte goes
	uint32_t tail; // where the next one comes from
	uint32_t n;	   // bytes in it
	int32_t readers;
	int32_t writers;
	task::task* rd_wait; // a reader parked because there is nothing to read
	task::task* wr_wait; // a writer parked because there is no room to write
	int open;
};

pipebuf g_pipes[kMaxPipe];

struct memwin {
	uint8_t* buf;
	uint32_t cap;
	uint32_t wpos;
};

} // namespace

static pipebuf* pipe_of(node* n) { return (pipebuf*)n->internal; }

int fd_open(const char* path, uint32_t flags) {
	ofile* g_fd = fds();
	if (!g_fd)
		return -1;
	if (!path)
		return -1;

	if (flags & O_CREAT) {
		char pp[256];
		if (fs::resolve(path, task::cwd(), pp, sizeof pp) && !resolve(path)) {
			if (!fs::write_file(task::cwd(), path, nullptr, 0, O_TRUNC))
				return -1;
		}
	}
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

static void fd_unshare(const ofile* f) {
	if (!f || f->n.type != kTypeFifo)
		return;
	pipebuf* p = (pipebuf*)f->n.internal;
	if (!p)
		return;
	const bool write_end = (f->flags & kPipeWriteEnd) != 0;
	if (write_end)
		--p->writers;
	else
		--p->readers;
	if (p->writers < 0)
		p->writers = 0;
	if (p->readers < 0)
		p->readers = 0;

	// a party waiting on the far side is waiting for a count to move, so whoever
	// just moved it has to say so
	if (write_end && !p->writers && p->rd_wait) {
		task::unblock(p->rd_wait);
		p->rd_wait = nullptr;
	}
	if (!write_end && !p->readers && p->wr_wait) {
		task::unblock(p->wr_wait);
		p->wr_wait = nullptr;
	}

	if (!p->writers && !p->readers)
		p->open = 0;
}

static void close_in(task::task* t, int fd) {
	ofile* g_fd = t ? &t->fd[0] : nullptr;
	if (!g_fd || fd < 0 || fd >= kMaxFd)
		return;

	if (g_fd[fd].n.type == kTypeMem && g_fd[fd].n.internal) {
		((memwin*)g_fd[fd].n.internal)->buf = nullptr;
	}
	fd_unshare(&g_fd[fd]);
	memset(&g_fd[fd], 0, sizeof g_fd[fd]);
}

int fd_close(int fd) {
	close_in(task::g_current, fd);
	return 0;
}

void fd_close_all(task::task* t) {
	if (!t)
		return;
	for (int i = 0; i < kMaxFd; ++i)
		if (t->fd[i].n.type)
			close_in(t, i);
}

// a new process has been handed a copy of this table, so it now holds every pipe end in it as well
void fd_share(const ofile* fd) {
	if (!fd || fd->n.type != kTypeFifo)
		return;
	pipebuf* p = (pipebuf*)fd->n.internal;
	if (!p)
		return;
	if (fd->flags & kPipeWriteEnd)
		++p->writers;
	else
		++p->readers;
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

// the pipes
namespace {

int pipe_rd(node* n, void* buf, uint32_t, uint32_t len) {
	pipebuf* p = pipe_of(n);
	if (!p)
		return -1;

	while (p->n == 0 && p->writers)
		task::block_on(&p->rd_wait);
	if (p->n == 0)
		return 0; // every writer has gone

	uint32_t got = 0;
	while (got < len && p->n) {
		((uint8_t*)buf)[got++] = p->buf[p->tail];
		p->tail = (p->tail + 1u) % kPipeBytes;
		--p->n;
	}
	// there is room again, so a writer parked on a full pipe can go
	if (p->wr_wait) {
		task::unblock(p->wr_wait);
		p->wr_wait = nullptr;
	}
	return (int)got;
}

int pipe_wr(node* n, const void* buf, uint32_t, uint32_t len) {
	pipebuf* p = pipe_of(n);
	if (!p)
		return -1;
	const uint8_t* s = (const uint8_t*)buf;
	uint32_t put = 0;
	while (put < len) {
		while (p->n == kPipeBytes && p->readers)
			task::block_on(&p->wr_wait);
		if (p->n == kPipeBytes)
			return -1;
		p->buf[p->head] = s[put++];
		p->head = (p->head + 1u) % kPipeBytes;
		++p->n;

		if (p->rd_wait) {
			task::unblock(p->rd_wait);
			p->rd_wait = nullptr;
		}
	}
	return (int)put;
}

} // namespace

int pipe(int ends[2]) {
	ofile* g_fd = fds();
	if (!g_fd || !ends)
		return -1;
	pipebuf* p = nullptr;
	for (int i = 0; i < kMaxPipe; ++i)
		if (!g_pipes[i].open) {
			p = &g_pipes[i];
			break;
		}
	if (!p)
		return -1;
	memset(p, 0, sizeof *p);
	p->open = 1;
	p->readers = 1;
	p->writers = 1;

	int r = -1;
	int w = -1;
	for (int i = 0; i < kMaxFd; ++i)
		if (g_fd[i].n.type == 0) {
			if (r < 0)
				r = i;
			else if (w < 0) {
				w = i;
				break;
			}
		}
	if (r < 0 || w < 0)
		return -1;
	for (int i = 0; i < kMaxFd; ++i) {
		ofile* e = &g_fd[i];
		if (i != r && i != w)
			continue;
		memset(e, 0, sizeof *e);
		e->n.type = kTypeFifo;
		e->n.internal = p;
		e->n.read = pipe_rd;
		e->n.write = pipe_wr;
		e->n.name = e->n.name_buf;
		strncpy(e->n.name_buf, i == r ? "pipe_r" : "pipe_w", sizeof e->n.name_buf - 1);
		e->flags = (i == r) ? 0u : kPipeWriteEnd;
	}
	ends[0] = r;
	ends[1] = w;
	return 0;
}

// put a descriptor of ours into another processes table as `child_fd`
int fd_install(task::task* into, int child_fd, int from) {
	if (!into || from < 0 || from >= kMaxFd || child_fd < 0 || child_fd >= kMaxFd)
		return -1;
	ofile* mine = fds();
	if (!mine || !mine[from].n.type)
		return -1;
	ofile* theirs = &into->fd[child_fd];

	fd_unshare(theirs);
	memcpy(theirs, &mine[from], sizeof *theirs);
	theirs->n.name = theirs->n.name_buf;
	fd_share(theirs);
	return child_fd;
}

int mem_wr(node* n, const void* buf, uint32_t, uint32_t len) {
	memwin* m = (memwin*)n->internal;
	if (!m)
		return -1;
	uint32_t room = m->wpos < m->cap ? m->cap - m->wpos : 0;
	const uint32_t put = len < room ? len : room;
	memcpy(m->buf + m->wpos, buf, put);
	m->wpos += put;

	return (int)put;
}

int fd_mem(void* buf, uint32_t size) {
	ofile* g_fd = fds();
	static memwin wins[4];
	if (!g_fd || !buf || size == 0)
		return -1;
	memwin* m = nullptr;
	for (int i = 0; i < 4; ++i)
		if (!wins[i].buf) {
			m = &wins[i];
			break;
		}
	if (!m)
		return -1;
	m->buf = (uint8_t*)buf;
	m->cap = size;
	m->wpos = 0;
	for (int i = 0; i < kMaxFd; ++i) {
		if (g_fd[i].n.type)
			continue;
		ofile* e = &g_fd[i];
		memset(e, 0, sizeof *e);
		// write only
		e->n.type = kTypeMem;
		e->n.internal = m;
		e->n.write = mem_wr;
		e->n.name = e->n.name_buf;
		strncpy(e->n.name_buf, "mem", sizeof e->n.name_buf - 1);
		return i;
	}
	m->buf = nullptr;
	return -1;
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

int ioctl(int fd, uint32_t req, void* arg) {
	ofile* g_fd = fds();
	if (!g_fd)
		return -kErrBadf;
	if (fd < 0 || fd >= kMaxFd)
		return -kErrBadf;
	ofile* e = &g_fd[fd];
	if (!e->n.type)
		return -kErrBadf;
	if (!e->n.ctl)
		return -kErrInval;
	return e->n.ctl(&e->n, req, arg);
}

// point one fd at the same open file, vacating the target first
int next_free() {
	ofile* g_fd = fds();
	if (!g_fd)
		return -1;
	for (int i = 0; i < kMaxFd; ++i)
		if (!g_fd[i].n.type)
			return i;
	return -1;
}

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

	fd_share(&g_fd[nw]);
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
const char* path() { return "/bin"; }

// first PATH entry that holds the name wins
bool find_in_path(const char* name, char* out, uint32_t outsz) {
	if (!name || !out || outsz < 2)
		return false;
	const char* p = path();
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

	memset(&g_tty, 0, sizeof g_tty);
	g_tty.flags = kTtyCanon | kTtyEcho | kTtySig;

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