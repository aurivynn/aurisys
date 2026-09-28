#pragma once

#include "syscall.h"

#include <stdint.h>

namespace task {
struct task;
}

namespace vfs {

struct node;
typedef int (*read_fn)(node* n, void* buf, uint32_t off, uint32_t len);
typedef int (*write_fn)(node* n, const void* buf, uint32_t off, uint32_t len);
typedef int (*readdir_fn)(node* n, uint32_t index, node* out);
typedef node* (*find_fn)(node* n, const char* name);
typedef int (*ctl_fn)(node* n, uint32_t req, void* arg);

constexpr uint8_t kTypeFile = 1;
constexpr uint8_t kTypeDir = 2;
constexpr uint8_t kTypeChar = 3;

struct node {
	const char* name; // points into name_buf
	uint32_t inode;	  // fs identity, 0 for pure virtual nodes
	uint8_t type;	  // one of the kType bits
	uint8_t pad;
	uint16_t pad2;
	uint32_t size; // bytes

	node* parent;
	node* mount;
	void* internal;

	read_fn read;
	write_fn write;
	readdir_fn readdir;
	find_fn find_child;
	ctl_fn ctl; // null on anything that has nothing to control

	char name_buf[64]; // the name lives in here
};

bool init();					 // ext4 root plus devfs at /dev plus the fd table
node* resolve(const char* path); // relative paths resolve against the calling process cwd
node* root();
int readdir(node* n, uint32_t index, node* out); // out filled, 0 ok, neg end
bool mount(node* tree, const char* at);

struct ofile {
	node n;
	uint32_t pos;
	uint32_t flags;
};

constexpr int kMaxFd = 16;
constexpr uint8_t kTypeFifo = 4; // a pipe end
constexpr uint8_t kTypeMem = 5;	 // a window onto memory the caller owns

int fd_open(const char* path, uint32_t flags);
int fd_close(int fd);

void fd_close_all(task::task* t);
int fd_read(int fd, void* buf, uint32_t len);
int fd_write(int fd, const void* buf, uint32_t len);
int lseek(int fd, int off, int whence); // 0 set, 1 cur, 2 end, new offset out
int ioctl(int fd, uint32_t req, void* arg);
int dup2(int old, int nw); // point nw at old, closing nw first
int next_free();		   // the lowest unused descriptor or -1
int fd_install(task::task* into, int child_fd, int from);

int fd_mem(void* buf, uint32_t size);
node* fd_node(int fd);

void fd_close_all(task::task* t);

void fd_share(const ofile* fd);

int pipe(int fds[2]);

constexpr int kMaxPipe = 8;
constexpr uint32_t kPipeBytes = 4096;

// a private bit on the ofile saying which end of a pipe it is.
constexpr uint32_t kPipeWriteEnd = 0x10000u;

const char* path();												// the PATH string, colon separated
bool find_in_path(const char* name, char* out, uint32_t outsz); // first hit wins

} // namespace vfs