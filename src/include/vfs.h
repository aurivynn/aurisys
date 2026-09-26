#pragma once

#include <stdint.h>

namespace vfs {

struct node;
typedef int (*read_fn)(node* n, void* buf, uint32_t off, uint32_t len);
typedef int (*write_fn)(node* n, const void* buf, uint32_t off, uint32_t len);
typedef int (*readdir_fn)(node* n, uint32_t index, node* out);
typedef node* (*find_fn)(node* n, const char* name);

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

	char name_buf[64]; // the name lives in here
};

bool init();					 // ext4 root plus devfs at /dev plus the fd table
node* resolve(const char* path); // relative paths resolve against fs::cwd
node* root();
int readdir(node* n, uint32_t index, node* out); // out filled, 0 ok, neg end
bool mount(node* tree, const char* at);

struct ofile {
	node n;
	uint32_t pos;
	uint32_t flags;
};

constexpr int kMaxFd = 16;
constexpr uint32_t kFdAppend = 1;
constexpr uint32_t kFdTrunc = 2;

int fd_open(const char* path, uint32_t flags);
int fd_close(int fd);
int fd_read(int fd, void* buf, uint32_t len);
int fd_write(int fd, const void* buf, uint32_t len);
int lseek(int fd, int off, int whence); // 0 set, 1 cur, 2 end, new offset out
int dup2(int old, int nw);				// point nw at old, closing nw first
node* fd_node(int fd);

const char* path();												// the PATH string, colon separated
bool find_in_path(const char* name, char* out, uint32_t outsz); // first hit wins

} // namespace vfs