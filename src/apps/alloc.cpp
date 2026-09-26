// the userspace allocator
#include "lib/alloc.h"

#include "syscall.h"

#include <stdint.h>

extern "C" void* memset(void* dst, int c, size_t n);

// the gate itself
static int gate(uint32_t num, uint32_t a) {
	int out;
	asm volatile("int $0x80" : "=a"(out) : "a"(num), "b"(a) : "memory");
	return out;
}

namespace {

using alloc::align_up;
using alloc::Block;
using alloc::block_of;
using alloc::kAlign;
using alloc::kMinBlock;
using alloc::payload_of;

uint8_t* g_next = nullptr; // next byte never handed out
uint8_t* g_end = nullptr;  // one past the last byte we own
Block* g_free = nullptr;   // returned blocks threaded by alloc::Block::next

void release_tail() {
	const uint32_t left = (uint32_t)(g_end - g_next);
	if (left < kMinBlock)
		return;
	Block* tail = (Block*)g_next;
	tail->size = left;
	tail->next = g_free;
	g_free = tail;
	g_next = g_end;
}

// take `bytes` from the arena
uint8_t* carve(uint32_t bytes) {
	if ((uint32_t)(g_end - g_next) >= bytes) {
		uint8_t* p = g_next;
		g_next += bytes;
		return p;
	}
	const uint32_t before = (uint32_t)gate(SYS_sbrk, 0);
	if (before == 0)
		return nullptr;
	// a page multiple with room to spare, so the next few allocations do not each cost a syscall
	const uint32_t want = align_up(bytes, 4096u) + 4096u;
	const uint32_t got = (uint32_t)gate(SYS_sbrk, (uint32_t)(int32_t)want);
	if (got == 0)
		return nullptr;					// the arena ceiling
	const uint32_t have = got - before; // what actually arrived
	g_next = (uint8_t*)(uintptr_t)before;
	g_end = (uint8_t*)(uintptr_t)got;
	if (have >= bytes) {
		uint8_t* p = g_next;
		g_next += bytes;
		return p;
	}
	// short of the request
	g_next = g_end;
	release_tail();
	return nullptr;
}

} // namespace

extern "C" void* malloc(size_t n) {
	if (n == 0)
		n = 1;
	const uint32_t bytes = align_up((uint32_t)n + (uint32_t)sizeof(Block));
	release_tail();
	Block** link = &g_free;
	while (*link) {
		Block* b = *link;
		if (b->size < bytes) {
			link = &b->next;
			continue;
		}
		if (b->size >= bytes + sizeof(Block)) {
			Block* tail = (Block*)((uint8_t*)b + bytes);
			tail->size = b->size - bytes;
			tail->next = b->next;
			b->size = bytes;
			*link = tail;
		} else {
			*link = b->next; // exact fit!!
		}
		return payload_of(b);
	}
	Block* b = (Block*)carve(bytes);
	if (!b)
		return nullptr;
	b->size = bytes;
	b->next = nullptr;
	return payload_of(b);
}

extern "C" void free(void* p) {
	if (!p)
		return;
	Block* b = block_of(p);
	if (b->size < kMinBlock)
		return;
	Block* before = nullptr;
	Block* prev = nullptr;
	Block* cur = g_free;
	while (cur && (uint8_t*)cur < (uint8_t*)b) {
		before = prev;
		prev = cur;
		cur = cur->next;
	}
	if (cur && (uint8_t*)b + b->size == (uint8_t*)cur) {
		b->size += cur->size;
		cur = cur->next;
	}
	if (prev && (uint8_t*)prev + prev->size == (uint8_t*)b) {
		prev->size += b->size;
		b = prev;
		prev = before;
	}
	b->next = cur;
	if (prev)
		prev->next = b;
	else
		g_free = b;
}

extern "C" void* calloc(size_t n, size_t size) {
	if (n && size > 0xFFFFFFFFu / n)
		return nullptr;
	const size_t total = n * size;
	void* p = malloc(total);
	if (p)
		memset(p, 0, total);
	return p;
}

extern "C" void* realloc(void* p, size_t n) {
	if (!p)
		return malloc(n);
	if (n == 0) {
		free(p);
		return nullptr;
	}
	Block* b = block_of(p);
	const uint32_t want = align_up((uint32_t)n + (uint32_t)sizeof(Block));
	if ((uint8_t*)b + b->size == g_next) {
		uint8_t* end = (uint8_t*)b + want;
		if (end <= g_end) {
			b->size = want;
			g_next = end;
			return p;
		}
	}
	void* fresh = malloc(n);
	if (!fresh)
		return nullptr;
	uint8_t* d = (uint8_t*)fresh;
	const uint8_t* s = (const uint8_t*)p;
	const uint32_t live = b->size - (uint32_t)sizeof(Block);
	const uint32_t copy = (uint32_t)n < live ? (uint32_t)n : live;
	for (uint32_t i = 0; i < copy; ++i)
		d[i] = s[i];
	free(p);
	return fresh;
}
