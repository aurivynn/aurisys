#include "fs.h"

#include "drivers/ata.h"
#include "lib/mem.h"
#include "lib/str.h"

#include <stdint.h>

namespace fs {

namespace {

constexpr uint32_t kSbMagic = 0xEF53;
constexpr uint32_t kMaxBlockSize = 4096;
constexpr uint16_t kExtMagic = 0xF30A;
constexpr uint32_t kExtentsFl = 0x80000;
constexpr int kMaxDepth = 4;
constexpr uint32_t kRootIno = 2;

bool g_mounted = false;
uint32_t g_part_lba;		  // partition start
uint32_t g_bs;				  // block size
uint32_t g_blocks;			  // s_blocks_count
uint32_t g_inodes;			  // s_inodes_count
uint32_t g_first_data;		  // s_first_data_block
uint32_t g_bpg;				  // s_blocks_per_group
uint32_t g_ipg;				  // s_inodes_per_group
uint32_t g_inode_size;		  // s_inode_size (256 here)
uint32_t g_ipb;				  // inodes per block
uint32_t g_desc_block;		  // block of the group descriptor table
uint32_t g_desc_count;		  // groups on this fs
uint32_t g_bbb, g_ibb, g_itb; // group 0
uint32_t g_free_blocks, g_free_inodes;
uint32_t g_feat_compat, g_feat_incompat, g_feat_ro;

uint8_t g_sb[1024];			 // the superblock
uint8_t g_b1[kMaxBlockSize]; // general block scratch
uint8_t g_b2[kMaxBlockSize]; // dirent block scratch
uint8_t g_ino[512];			 // the current inode image
char g_name[256];			 // transient name handed to the dirent callback
char g_cwd[128];

inline uint16_t r16(const uint8_t* p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
inline uint32_t r32(const uint8_t* p) {
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
inline void w16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}
inline void w32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

bool read_block(uint32_t blk, void* buf) {
	if (blk >= g_blocks)
		return false;
	return ata::read_sectors(g_part_lba + blk * (g_bs >> 9), g_bs >> 9, buf);
}

bool write_block(uint32_t blk, const void* buf) {
	if (blk >= g_blocks)
		return false;
	return ata::write_sectors(g_part_lba + blk * (g_bs >> 9), g_bs >> 9, buf);
}

bool load_inode(uint32_t ino) {
	if (ino == 0 || ino > g_inodes)
		return false;
	if (!read_block(g_itb + (ino - 1) / g_ipb, g_b1))
		return false;
	memcpy(g_ino, g_b1 + ((ino - 1) % g_ipb) * g_inode_size, g_inode_size);
	return true;
}

bool inode_is_dir() { return (r16(g_ino) & 0xF000) == 0x4000; }
bool inode_is_file() { return (r16(g_ino) & 0xF000) == 0x8000; }

uint32_t inode_size() {
	const uint32_t lo = r32(g_ino + 4);
	return r32(g_ino + 0x6C) ? 0xFFFFFFFFu : lo;
}

bool extent_walk(const uint8_t* node, uint32_t lblock, uint32_t* phys, int depth) {
	if (depth > kMaxDepth || r16(node) != kExtMagic)
		return false;
	const uint16_t entries = r16(node + 2);
	const uint16_t node_depth = r16(node + 6);

	if (node_depth == 0) { // leaf: the extents are here
		for (uint16_t i = 0; i < entries; ++i) {
			const uint8_t* e = node + 12 + i * 12;
			const uint32_t eb = r32(e);
			const uint16_t len = r16(e + 4) & 0x7FFF; // top bit = unwritten
			if (lblock < eb || lblock >= eb + len)
				continue;
			const uint32_t start = (uint32_t)r16(e + 6) << 16 | r32(e + 8);
			*phys = start + (lblock - eb);
			return true;
		}
		return false;
	}

	for (uint16_t i = 0; i < entries; ++i) {
		const uint8_t* e = node + 12 + i * 12;
		const uint32_t eb = r32(e);
		const uint32_t next = (i + 1 < entries) ? r32(e + 12) : 0xFFFFFFFF;
		if (lblock < eb)
			return false;
		if (lblock >= next)
			continue;
		const uint32_t child = (uint32_t)r16(e + 8) << 16 | r32(e + 4);
		if (!read_block(child, g_b1))
			return false;
		return extent_walk(g_b1, lblock, phys, depth + 1);
	}
	return false;
}

bool indirect_walk(uint32_t lblock, uint32_t* phys) {
	const uint32_t per = g_bs / 4;
	constexpr uint32_t kDirect = 12;
	if (lblock < kDirect) {
		const uint32_t p = r32(g_ino + 0x28 + lblock * 4);
		return p != 0 && p < g_blocks ? (*phys = p, true) : false;
	}
	lblock -= kDirect;
	if (lblock < per) {
		const uint32_t ind = r32(g_ino + 0x28 + kDirect * 4);
		if (ind == 0 || ind >= g_blocks || !read_block(ind, g_b1))
			return false;
		const uint32_t p = r32(g_b1 + lblock * 4);
		return p != 0 && p < g_blocks ? (*phys = p, true) : false;
	}
	lblock -= per;
	if (lblock < per * per) {
		const uint32_t dbl = r32(g_ino + 0x28 + (kDirect + 1) * 4);
		if (dbl == 0 || dbl >= g_blocks || !read_block(dbl, g_b1))
			return false;
		const uint32_t sn = r32(g_b1 + (lblock / per) * 4);
		if (sn == 0 || sn >= g_blocks || !read_block(sn, g_b1))
			return false;
		const uint32_t p = r32(g_b1 + (lblock % per) * 4);
		return p != 0 && p < g_blocks ? (*phys = p, true) : false;
	}
	return false;
}

bool map_lblock(uint32_t lblock, uint32_t* phys) {
	if (r32(g_ino + 0x20) & kExtentsFl)
		return extent_walk(g_ino + 0x28, lblock, phys, 0);
	return indirect_walk(lblock, phys);
}

uint32_t g_hint = 0;

bool alloc_blocks(uint32_t n, uint32_t* first) {
	if (n == 0 || n > g_blocks || !read_block(g_bbb, g_b1))
		return false;
	uint32_t run = 0, start = 0;
	for (uint32_t i = 0; i < g_blocks; ++i) {
		const uint32_t bit = (g_hint + i) % g_blocks;
		const uint32_t byte = bit / 8;
		if (g_b1[byte] & (1u << (bit % 8))) {
			run = 0;
			continue;
		}
		if (run == 0)
			start = bit;
		if (++run == n)
			break;
	}
	if (run < n)
		return false;
	for (uint32_t k = 0; k < n; ++k) {
		const uint32_t bit = start + k;
		g_b1[bit / 8] |= (uint8_t)(1u << (bit % 8));
	}
	if (!write_block(g_bbb, g_b1))
		return false;
	*first = start;
	g_hint = (start + n) % g_blocks;
	return true;
}

// first free inode in group 0
uint32_t alloc_inode() {
	if (!read_block(g_ibb, g_b1))
		return 0;
	for (uint32_t i = 0; i < g_ipg; ++i) {
		if (g_b1[i / 8] & (1u << (i % 8)))
			continue;
		g_b1[i / 8] |= (uint8_t)(1u << (i % 8));
		if (!write_block(g_ibb, g_b1))
			return 0;
		return i + 1;
	}
	return 0;
}

uint32_t dirent_used(uint32_t name_len) { return 8 + ((name_len + 3u) & ~3u); }

bool dir_add_entry(uint32_t dir_ino, const char* name, uint32_t ino, uint8_t type) {
	const uint32_t name_len = (uint32_t)strlen(name);
	const uint32_t need = dirent_used(name_len);
	if (need > g_bs)
		return false;

	if (!load_inode(dir_ino) || !inode_is_dir()) // loads dir_ino into g_ino
		return false;
	uint32_t size = inode_size();
	const uint32_t nblk = (size + g_bs - 1) / g_bs;

	for (uint32_t b = 0; b < nblk; ++b) {
		if (!load_inode(dir_ino) || !inode_is_dir())
			return false; // reload
		uint32_t phys;
		if (!map_lblock(b, &phys) || !read_block(phys, g_b2))
			return false;
		uint32_t off = 0, last_off = 0, last_used = 0;
		while (off + 8 <= g_bs) {
			const uint16_t rc = r16(g_b2 + off + 4);
			if (rc < 8 || off + rc > g_bs)
				return false; // malformed dir
			last_used = dirent_used(g_b2[off + 6]);
			last_off = off;
			off += rc;
		}
		const uint32_t slack = (g_bs - last_off) - last_used;
		if (slack >= need) {
			// close out the tail entry
			const uint32_t new_off = last_off + last_used;
			w16(g_b2 + last_off + 4, (uint16_t)last_used);
			w32(g_b2 + new_off, ino);
			w16(g_b2 + new_off + 4, (uint16_t)(g_bs - new_off));
			g_b2[new_off + 6] = (uint8_t)name_len;
			g_b2[new_off + 7] = type;
			memcpy(g_b2 + new_off + 8, name, name_len);
			return write_block(phys, g_b2);
		}
	}

	uint32_t phys;
	if (!alloc_blocks(1, &phys))
		return false;
	memset(g_b2, 0, g_bs);
	w32(g_b2, 0); // empty directory entry filling the block
	w16(g_b2 + 4, (uint16_t)g_bs);
	g_b2[6] = 0;
	if (!write_block(phys, g_b2))
		return false;

	if (!load_inode(dir_ino) || !inode_is_dir())
		return false;
	uint16_t entries = r16(g_ino + 0x2A);
	if (entries >= 4)
		return false; // no extent tree
	const uint32_t ee = 0x34 + (uint32_t)entries * 12;
	w32(g_ino + ee, nblk); // this block is logical block #nblk
	w16(g_ino + ee + 4, 1);
	w32(g_ino + ee + 8, phys);
	w16(g_ino + 0x2A, (uint16_t)(entries + 1));
	w32(g_ino + 4, size + g_bs); // dir got bigger
	w32(g_ino + 0x1C, r32(g_ino + 0x1C) + (g_bs >> 9));

	w32(g_b2, ino);
	w16(g_b2 + 4, (uint16_t)g_bs);
	g_b2[6] = (uint8_t)name_len;
	g_b2[7] = type;
	memcpy(g_b2 + 8, name, name_len);
	if (!write_block(phys, g_b2))
		return false;

	// persist the grown dir inode
	if (!read_block(g_itb + (dir_ino - 1) / g_ipb, g_b1))
		return false;
	memcpy(g_b1 + ((dir_ino - 1) % g_ipb) * g_inode_size, g_ino, g_inode_size);
	return write_block(g_itb + (dir_ino - 1) / g_ipb, g_b1);
}

bool commit_counts(int used_blocks, int used_inodes, int used_dirs) {
	if (used_blocks == 0 && used_inodes == 0 && used_dirs == 0)
		return true;
	uint32_t stride = r16(g_sb + 0xFE);
	if (stride < 32)
		stride = 32;
	if (!read_block(g_desc_block, g_b1))
		return false;
	int fb = (int)r16(g_b1 + 0x0C) - used_blocks;
	int fi = (int)r16(g_b1 + 0x0E) - used_inodes;
	int fd = (int)r16(g_b1 + 0x10) + used_dirs; // dirs count up, not down
	if (fb < 0 || fi < 0 || fd < 0)
		return false;
	w16(g_b1 + 0x0C, (uint16_t)fb);
	w16(g_b1 + 0x0E, (uint16_t)fi);
	w16(g_b1 + 0x10, (uint16_t)fd);
	if (!write_block(g_desc_block, g_b1))
		return false;
	g_free_blocks = (uint32_t)((int)g_free_blocks - used_blocks);
	g_free_inodes = (uint32_t)((int)g_free_inodes - used_inodes);
	w32(g_sb + 0x0C, g_free_blocks);
	w32(g_sb + 0x10, g_free_inodes);
	return ata::write_sectors(g_part_lba + 2, 2, g_sb);
}

// push the inode image in g_ino
bool store_inode(uint32_t ino) {
	const uint32_t tbl = g_itb + (ino - 1) / g_ipb;
	if (!read_block(tbl, g_b1))
		return false;
	memcpy(g_b1 + ((ino - 1) % g_ipb) * g_inode_size, g_ino, g_inode_size);
	return write_block(tbl, g_b1);
}

} // namespace

bool mount(uint32_t disk_lba) {
	uint8_t mbr[512];
	if (!ata::read_sector(disk_lba, mbr) || r16(mbr + 510) != 0xAA55)
		return false;
	uint32_t part = 0;
	for (int i = 0; i < 4 && part == 0; ++i) {
		const uint8_t* e = mbr + 0x1BE + i * 16;
		if (e[4] == 0x83)
			part = r32(e + 8);
	}
	if (part == 0)
		return false;
	g_part_lba = part;

	if (!ata::read_sectors(g_part_lba + 2, 2, g_sb) || r16(g_sb + 0x38) != kSbMagic)
		return false;

	const uint32_t log = r32(g_sb + 0x18);
	if (log > 12)
		return false;
	g_bs = 1024u << log;
	if (g_bs > kMaxBlockSize)
		return false; // we only buffer 4k
	g_first_data = r32(g_sb + 0x14);
	g_blocks = r32(g_sb + 0x04);
	g_inodes = r32(g_sb + 0x00);
	g_bpg = r32(g_sb + 0x20);
	g_ipg = r32(g_sb + 0x28);

	const uint16_t ino_sz = r16(g_sb + 0x58);
	if (ino_sz < 128 || ino_sz > 512 || (ino_sz & (ino_sz - 1)))
		return false;
	g_inode_size = ino_sz;
	g_ipb = g_bs / g_inode_size;
	if (g_ipb == 0 || g_bpg == 0 || g_ipg == 0 || g_blocks == 0)
		return false;

	g_desc_block = g_first_data + 1;
	g_desc_count = (g_blocks + g_bpg - 1) / g_bpg;
	uint32_t stride = r16(g_sb + 0xFE);
	if (stride < 32)
		stride = 32;

	if (!read_block(g_desc_block, g_b1))
		return false;
	g_bbb = r32(g_b1 + 0 * stride);
	g_ibb = r32(g_b1 + 4);
	g_itb = r32(g_b1 + 8);

	g_feat_compat = r32(g_sb + 0x5C);
	g_feat_incompat = r32(g_sb + 0x60);
	g_feat_ro = r32(g_sb + 0x64);
	g_free_blocks = r32(g_sb + 0x0C);
	g_free_inodes = r32(g_sb + 0x10);

	g_mounted = true;
	if (!load_inode(kRootIno) || !inode_is_dir()) {
		g_mounted = false;
		return false;
	}
	g_cwd[0] = '/';
	g_cwd[1] = 0;
	return true;
}

bool lookup_abs(const char* path, uint32_t* ino_out) {
	if (!g_mounted || !path)
		return false;
	char p[256];
	strncpy(p, path, sizeof p - 1);
	p[sizeof p - 1] = 0;

	struct ctx_t {
		const char* want;
		uint32_t ino;
		bool found;
	};
	auto find = [](const char* name, uint32_t ino, uint8_t type, void* ctx) -> bool {
		(void)type;
		ctx_t* c = (ctx_t*)ctx;
		if (strcmp(name, c->want) == 0) {
			c->ino = ino;
			c->found = true;
			return false; // done
		}
		return true;
	};

	uint32_t ino = kRootIno;
	char* tok = p;
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

		ctx_t c = {tok, 0, false};
		const int n = list_dir(ino, find, &c);
		*end = saved;
		if (n < 0 || !c.found)
			return false;
		ino = c.ino;
		tok = end + 1;
	}
	*ino_out = ino;
	return true;
}

bool resolve(const char* path, char* buf, uint32_t bufsz) {
	if (!path || !buf || bufsz < 2)
		return false;
	char tmp[256];
	if (path[0] == '/') {
		tmp[0] = 0;
	} else {
		strncpy(tmp, g_cwd, sizeof tmp - 1);
		tmp[sizeof tmp - 1] = 0;
	}
	int out = (int)strlen(tmp);
	const char* s = path;
	while (*s) {
		while (*s == '/')
			++s;
		if (!*s)
			break;
		const char* e = s;
		while (*e && *e != '/')
			++e;
		const int clen = (int)(e - s);
		if (clen == 1 && s[0] == '.') {
			s = e;
			continue;
		}
		if (clen == 2 && s[0] == '.' && s[1] == '.') {
			while (out > 1 && tmp[out - 1] == '/')
				--out;
			while (out > 0 && tmp[out - 1] != '/')
				--out;
			if (out > 1)
				--out;
			s = e;
			continue;
		}
		if (out == 0)
			tmp[out++] = '/';
		else if (tmp[out - 1] != '/')
			tmp[out++] = '/';
		if (out + clen >= (int)sizeof tmp)
			return false;
		memcpy(tmp + out, s, (size_t)clen);
		out += clen;
		s = e;
	}
	const int n = out == 0 ? 1 : out;
	if ((uint32_t)n + 1 > bufsz)
		return false;
	if (out == 0) {
		buf[0] = '/';
		buf[1] = 0;
		return true;
	}
	memcpy(buf, tmp, (size_t)n);
	buf[n] = 0;
	return true;
}

bool lookup(const char* path, uint32_t* ino_out) {
	if (!g_mounted || !path || !ino_out)
		return false;
	char abs[256];
	if (!resolve(path, abs, sizeof abs))
		return false;
	return lookup_abs(abs, ino_out);
}

const char* cwd() { return g_mounted ? g_cwd : "/"; }

bool chdir(const char* path) {
	if (!g_mounted)
		return false;
	char abs[256];
	if (!resolve(path, abs, sizeof abs))
		return false;
	uint32_t ino;
	if (!lookup(abs, &ino))
		return false;
	struct stat st;
	if (!getstat(ino, &st) || (st.mode & 0xF000) != 0x4000)
		return false;
	strncpy(g_cwd, abs, sizeof g_cwd - 1);
	g_cwd[sizeof g_cwd - 1] = 0;
	return true;
}

bool getstat(uint32_t ino, struct stat* out) {
	if (!g_mounted || !load_inode(ino) || !out)
		return false;
	out->ino = ino;
	out->size = inode_size();
	out->mode = r16(g_ino);
	out->blocks = r32(g_ino + 0x1C);
	out->links = r16(g_ino + 0x1A);
	return true;
}

uint32_t read(uint32_t ino, void* buf, uint32_t len, uint32_t off) {
	if (!g_mounted || !load_inode(ino) || !inode_is_file())
		return 0;
	const uint32_t size = inode_size();
	if (off >= size)
		return 0;
	if (len > size - off)
		len = size - off;
	uint8_t* out = (uint8_t*)buf;
	uint32_t done = 0;
	while (done < len) {
		const uint32_t lblock = (off + done) / g_bs;
		const uint32_t inblk = (off + done) % g_bs;
		uint32_t take = g_bs - inblk;
		if (take > len - done)
			take = len - done;
		uint32_t phys;
		if (!map_lblock(lblock, &phys) || !read_block(phys, g_b1))
			return done;
		memcpy(out + done, g_b1 + inblk, take);
		done += take;
	}
	return done;
}

int list_dir(uint32_t dir_ino, dirent_cb cb, void* ctx) {
	if (!g_mounted || !load_inode(dir_ino) || !inode_is_dir())
		return -1;
	const uint32_t nblk = (inode_size() + g_bs - 1) / g_bs;
	int count = 0;
	for (uint32_t b = 0; b < nblk; ++b) {
		if (!load_inode(dir_ino) || !inode_is_dir())
			return -1;
		uint32_t phys;
		if (!map_lblock(b, &phys) || !read_block(phys, g_b2))
			return -1;
		uint32_t off = 0;
		while (off + 8 <= g_bs) {
			const uint16_t reclen = r16(g_b2 + off + 4);
			if (reclen < 8 || off + reclen > g_bs)
				break;
			const uint32_t ino = r32(g_b2 + off);
			const uint8_t nlen = g_b2[off + 6];
			if (ino != 0 && nlen > 0 && nlen < sizeof g_name) {
				memcpy(g_name, g_b2 + off + 8, nlen);
				g_name[nlen] = 0;
				++count;
				if (!cb(g_name, ino, g_b2[off + 7], ctx))
					return count;
			}
			off += reclen;
		}
	}
	return count;
}

void summary(uint32_t* block_size, uint32_t* blocks, uint32_t* free_blocks, uint32_t* inodes, uint32_t* free_inodes,
			 uint32_t* feat_compat, uint32_t* feat_incompat, uint32_t* feat_ro) {
	*block_size = g_mounted ? g_bs : 0;
	*blocks = g_mounted ? g_blocks : 0;
	*free_blocks = g_mounted ? g_free_blocks : 0;
	*inodes = g_mounted ? g_inodes : 0;
	*free_inodes = g_mounted ? g_free_inodes : 0;
	*feat_compat = g_mounted ? g_feat_compat : 0;
	*feat_incompat = g_mounted ? g_feat_incompat : 0;
	*feat_ro = g_mounted ? g_feat_ro : 0;
}

// clear n block bitmap bits starting at first
bool free_blocks(uint32_t first, uint32_t n) {
	if (n == 0 || first + n > g_blocks)
		return false;
	if (!read_block(g_bbb, g_b1))
		return false;
	for (uint32_t k = 0; k < n; ++k) {
		const uint32_t bit = first + k;
		g_b1[bit / 8] &= (uint8_t)~(1u << (bit % 8));
	}
	return write_block(g_bbb, g_b1);
}

uint32_t write_blocks(const void* data, uint32_t len, uint32_t lstart) {
	const uint32_t total = (len + g_bs - 1) / g_bs;
	uint32_t done = 0;
	uint32_t used = 0;
	uint32_t ext = r16(g_ino + 0x2A);
	while (done < total) {
		const uint32_t want = total - done;
		const uint32_t chunk = want > 32767 ? 32767 : want;
		// bail before allocing a thing
		if (ext >= 4)
			return 0;
		uint32_t first;
		if (!alloc_blocks(chunk, &first))
			return 0;
		used += chunk;

		uint32_t d = done * g_bs;
		for (uint32_t i = 0; i < chunk; ++i) {
			memset(g_b1, 0, g_bs);
			const uint32_t rem = len - d;
			const uint32_t n = rem > g_bs ? g_bs : rem;
			memcpy(g_b1, (const uint8_t*)data + d, n);
			if (!write_block(first + i, g_b1))
				return 0;
			d += g_bs;
		}

		const uint32_t ee = 0x34 + ext * 12;
		w32(g_ino + ee, lstart + done); // ee_block
		w16(g_ino + ee + 4, (uint16_t)chunk);
		w32(g_ino + ee + 8, first);
		w16(g_ino + 0x2A, (uint16_t)(ext + 1));
		++ext;
		done += chunk;
	}
	return used;
}

struct ext_entry {
	uint32_t first, len;
};

// pull the flat inline extent list out of the inode in g_ino
bool inline_extents(ext_entry* out, uint32_t* nout) {
	if (!(r32(g_ino + 0x20) & kExtentsFl))
		return false;
	const uint16_t en = r16(g_ino + 0x2A);
	if (r16(g_ino + 0x2E) != 0 || en > 4)
		return false;
	for (uint16_t i = 0; i < en; ++i) {
		const uint8_t* e = g_ino + 0x34 + i * 12;
		out[i].first = (uint32_t)r16(e + 6) << 16 | r32(e + 8);
		out[i].len = r16(e + 4) & 0x7FFF;
	}
	*nout = en;
	return true;
}

bool write_file(const char* path, const void* data, uint32_t len, uint32_t flags) {
	if (!g_mounted || !path || (len && !data))
		return false;
	if ((flags & kWriteTrunc) && (flags & kWriteAppend))
		return false;

	char abs[256];
	if (!resolve(path, abs, sizeof abs))
		return false;

	char* name = abs;
	for (char* c = abs; *c; ++c)
		if (*c == '/')
			name = c + 1;
	if (*name == 0)
		return false;
	uint32_t parent_ino;
	{
		char* slash = name - 1;
		const char saved = *slash;
		*slash = 0;
		const bool ok = lookup(abs, &parent_ino);
		*slash = saved;
		if (!ok)
			return false;
	}

	uint32_t ino;
	const bool exists = lookup(abs, &ino);
	ext_entry old_ext[4];
	uint32_t old_n = 0;
	if (exists) {
		// create only refuses to touch a file thats already there
		if (!(flags & (kWriteTrunc | kWriteAppend)))
			return false;
		if (!load_inode(ino) || !inode_is_file())
			return false;		 // dirs do not get overwritten
		if (flags & kWriteTrunc) // remember the old blocks to free later
			if (!inline_extents(old_ext, &old_n))
				return false;
	} else {
		ino = alloc_inode();
		if (ino == 0)
			return false;
	}

	const bool append = exists && (flags & kWriteAppend);
	if (append) {
		// only plain extent files, same as everything we create
		if (!(r32(g_ino + 0x20) & kExtentsFl))
			return false;
	} else {
		// fresh inode image, done after any old extent scrape
		memset(g_ino, 0, sizeof g_ino);
		w16(g_ino, 0x81A4); // S_IFREG | 0644
		w32(g_ino + 4, len);
		w16(g_ino + 0x1A, 1); // links
		w32(g_ino + 0x20, kExtentsFl);
		w32(g_ino + 0x1C, 0); // i_blocks
		w16(g_ino + 0x28, kExtMagic);
		w16(g_ino + 0x2A, 0); // entries
		w16(g_ino + 0x2C, 4); // max entries that fit i_block
		w16(g_ino + 0x2E, 0); // depth 0
	}

	// no journal on this volume
	const uint32_t old_size = inode_size();
	uint32_t used = 0;
	if (append) {
		uint32_t d = 0;
		const uint32_t lstart = (old_size + g_bs - 1) / g_bs;
		if (old_size % g_bs != 0) {
			const uint32_t inblk = old_size % g_bs;
			const uint32_t n = (g_bs - inblk) > len ? len : (g_bs - inblk);
			uint32_t phys;
			if (!map_lblock(old_size / g_bs, &phys) || !read_block(phys, g_b2))
				return false;
			memcpy(g_b2 + inblk, data, n);
			if (!write_block(phys, g_b2))
				return false;
			d = n;
		}
		if (d < len) {
			used = write_blocks((const uint8_t*)data + d, len - d, lstart);
			if (used == 0)
				return false;
		}
	} else if (len) {
		used = write_blocks(data, len, 0);
		if (used == 0)
			return false;
		w32(g_ino + 0x1C, used * (g_bs >> 9)); // i_blocks
	}

	if (append) {
		w32(g_ino + 4, old_size + len);
		w32(g_ino + 0x1C, r32(g_ino + 0x1C) + used * (g_bs >> 9));
	}
	if (!store_inode(ino))
		return false;

	if (!exists) {
		if (!dir_add_entry(parent_ino, name, ino, 1))
			return false;
		return commit_counts((int)used, 1, 0);
	}
	uint32_t freed = 0;
	if (flags & kWriteTrunc) {
		for (uint32_t i = 0; i < old_n; ++i) {
			if (!free_blocks(old_ext[i].first, old_ext[i].len))
				return false;
			freed += old_ext[i].len;
		}
	}
	return commit_counts((int)used - (int)freed, 0, 0);
}

bool mkdir(const char* path) {
	if (!g_mounted || !path)
		return false;
	char abs[256];
	if (!resolve(path, abs, sizeof abs))
		return false;

	char* name = abs;
	for (char* c = abs; *c; ++c)
		if (*c == '/')
			name = c + 1;
	if (*name == 0)
		return false; // mkdir / is a no op

	uint32_t parent_ino;
	{
		char* slash = name - 1;
		const char saved = *slash;
		*slash = 0;
		const bool ok = lookup(abs, &parent_ino);
		*slash = saved;
		if (!ok)
			return false;
	}
	struct stat st;
	if (!getstat(parent_ino, &st) || (st.mode & 0xF000) != 0x4000)
		return false;
	uint32_t tmp;
	if (lookup(abs, &tmp))
		return false; // already there

	uint32_t ino = alloc_inode();
	if (ino == 0)
		return false;
	uint32_t phys;
	if (!alloc_blocks(1, &phys))
		return false;

	memset(g_ino, 0, sizeof g_ino);
	w16(g_ino, 0x41ED); // S_IFDIR | 0755
	w32(g_ino + 4, g_bs);
	w16(g_ino + 0x1A, 2); // links, one for . and one for ..
	w32(g_ino + 0x20, kExtentsFl);
	w32(g_ino + 0x1C, g_bs >> 9);
	w16(g_ino + 0x28, kExtMagic);
	w16(g_ino + 0x2A, 1);
	w16(g_ino + 0x2C, 4);
	w16(g_ino + 0x2E, 0);
	w32(g_ino + 0x34, 0);	 // ee_block 0
	w16(g_ino + 0x38, 1);	 // ee_len
	w32(g_ino + 0x3C, phys); // ee_start lo
	if (!store_inode(ino))
		return false;

	// seed the dir data block with . and .., rec_len runs to end of block
	memset(g_b2, 0, g_bs);
	w32(g_b2, ino); // .
	w16(g_b2 + 4, 12);
	g_b2[6] = 1;
	g_b2[7] = 2;
	g_b2[8] = '.';
	w32(g_b2 + 12, parent_ino); // ..
	w16(g_b2 + 16, (uint16_t)(g_bs - 12));
	g_b2[18] = 2;
	g_b2[19] = 2;
	g_b2[20] = '.';
	g_b2[21] = '.';
	if (!write_block(phys, g_b2))
		return false;

	// the parent gained a subdir
	if (!load_inode(parent_ino))
		return false;
	w16(g_ino + 0x1A, (uint16_t)(r16(g_ino + 0x1A) + 1));
	if (!store_inode(parent_ino))
		return false;

	if (!dir_add_entry(parent_ino, name, ino, 2))
		return false;
	return commit_counts(1, 1, 1);
}

bool rm(const char* path) {
	if (!g_mounted || !path)
		return false;
	char abs[256];
	if (!resolve(path, abs, sizeof abs))
		return false;

	char* name = abs;
	for (char* c = abs; *c; ++c)
		if (*c == '/')
			name = c + 1;
	if (*name == 0)
		return false; // rm / is a no no

	uint32_t parent_ino, ino;
	{
		char* slash = name - 1;
		const char saved = *slash;
		*slash = 0;
		const bool ok = lookup(abs, &parent_ino);
		*slash = saved;
		if (!ok)
			return false;
	}
	if (!lookup(abs, &ino))
		return false;

	struct stat st;
	if (!getstat(parent_ino, &st) || (st.mode & 0xF000) != 0x4000)
		return false;
	if (!load_inode(ino))
		return false;

	const bool is_dir = inode_is_dir();
	if (!is_dir && !inode_is_file())
		return false;
	if (is_dir) {
		// only empty dirs go. . and .. count as 2, more means children
		auto skip = [](const char*, uint32_t, uint8_t, void*) { return true; };
		if (list_dir(ino, skip, nullptr) > 2)
			return false;
	}

	if (!load_inode(parent_ino) || !inode_is_dir())
		return false;
	{
		const uint32_t nblk = (inode_size() + g_bs - 1) / g_bs;
		bool done = false;
		for (uint32_t b = 0; b < nblk && !done; ++b) {
			if (!load_inode(parent_ino) || !inode_is_dir())
				return false;
			uint32_t phys;
			if (!map_lblock(b, &phys) || !read_block(phys, g_b2))
				return false;
			uint32_t off = 0, prev = 0;
			while (off + 8 <= g_bs) {
				const uint16_t rc = r16(g_b2 + off + 4);
				if (rc < 8 || off + rc > g_bs)
					return false; // malformed dir
				if (r32(g_b2 + off) == ino) {
					w16(g_b2 + prev + 4, (uint16_t)(r16(g_b2 + prev + 4) + rc));
					if (!write_block(phys, g_b2))
						return false;
					done = true;
					break;
				}
				prev = off;
				off += rc;
			}
		}
		if (!done)
			return false;
	}

	ext_entry old[4];
	uint32_t old_n = 0;
	if (!load_inode(ino) || !inline_extents(old, &old_n))
		return false;
	uint32_t freed = 0;
	for (uint32_t i = 0; i < old_n; ++i) {
		if (!free_blocks(old[i].first, old[i].len))
			return false;
		freed += old[i].len;
	}

	if (is_dir) {
		if (!load_inode(parent_ino))
			return false;
		w16(g_ino + 0x1A, (uint16_t)(r16(g_ino + 0x1A) - 1)); // lost a subdir
		if (!store_inode(parent_ino))
			return false;
	}

	if (!read_block(g_ibb, g_b1))
		return false;
	const uint32_t bit = ino - 1;
	g_b1[bit / 8] &= (uint8_t)~(1u << (bit % 8));
	if (!write_block(g_ibb, g_b1))
		return false;
	if (!read_block(g_itb + (ino - 1) / g_ipb, g_b1))
		return false;
	memset(g_b1 + ((ino - 1) % g_ipb) * g_inode_size, 0, g_inode_size);
	if (!write_block(g_itb + (ino - 1) / g_ipb, g_b1))
		return false;

	return commit_counts(-(int)freed, -1, is_dir ? -1 : 0);
}

} // namespace fs