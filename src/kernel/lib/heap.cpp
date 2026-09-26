#include "lib/heap.h"

#include "lib/mem.h"

#include <stdint.h>

extern "C" char __kernel_end;

namespace {

constexpr uint32_t kAlign = 16;
constexpr uint32_t kMinBlock = 32;
constexpr uint32_t kPage = 4096;

// size packs three things into one word:
//   bit 31  magic, set on every header we own. lets a free spot a pointer
//           that is not ours instead of corrupting the list
//   bit 0   in use
//   bits 1-30  the real size, header included
constexpr uint32_t kMagic = 0x80000000u;
constexpr uint32_t kInUse = 0x1u;
constexpr uint32_t kSizeMask = ~(kMagic | kInUse);
constexpr uint32_t kSizeMax = kSizeMask >> 1;

struct block {
	uint32_t size;		// packed as above
	uint32_t prev_size; // size of the physically previous block, 0 if first
	block* next;
	block* prev;
};

static_assert(sizeof(block) == kAlign, "header must be exactly one align unit to keep payloads aligned");

uint32_t g_start = 0;
uint32_t g_end = 0;
block* g_head = nullptr; // every block in address order, used and free alike

size_t g_allocs = 0;
size_t g_frees = 0;
size_t g_coalesced = 0;
size_t g_frames = 0;
size_t g_rejected = 0; // frees we refused, should stay 0

inline uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1u) & ~(a - 1u); }
inline uint32_t align_down(uint32_t v, uint32_t a) { return v & ~(a - 1u); }

inline uint32_t bsize(const block* b) { return b->size & kSizeMask; }
inline bool in_use(const block* b) { return (b->size & kInUse) != 0; }
inline bool ours(const block* b) { return (b->size & kMagic) != 0; }

inline void mark(block* b, bool used) { b->size = bsize(b) | kMagic | (used ? kInUse : 0u); }

// carve b down to `bytes`
block* split(block* b, uint32_t bytes) {
	const uint32_t total = bsize(b);
	if (total < bytes || total - bytes < kMinBlock)
		return nullptr;
	block* tail = (block*)((uint8_t*)b + bytes);
	block* after = b->next;
	tail->size = (total - bytes) | kMagic;
	tail->prev_size = bytes;
	mark(tail, false);
	b->size = bytes | kMagic;
	b->next = tail;
	tail->prev = b;
	if (after)
		after->prev = tail;
	return tail;
}

block* find_fit(uint32_t bytes) {
	for (block* b = g_head; b; b = b->next)
		if (!in_use(b) && bsize(b) >= bytes)
			return b;
	return nullptr;
}

} // namespace

void heap_init(const bootinfo* bi) {
	g_start = align_up((uint32_t)&__kernel_end, kAlign);
	g_end = 0;
	// only the usable region that holds the kernel is ours to manage
	for (uint32_t i = 0; i < bi->mem_entries && i < 32; ++i) {
		const e820_entry* e = &bi->mem[i];
		if (e->type != 1)
			continue;
		const uint32_t base = (uint32_t)e->base;
		const uint32_t end = base + (uint32_t)e->len;
		if (base <= 0x100000u && end > 0x100000u && end > g_end)
			g_end = align_down(end, kAlign);
	}
	if (g_end <= g_start + kMinBlock)
		g_end = g_start + 0x10000; // nothing sane

	block* b = (block*)g_start;
	b->prev_size = 0;
	b->next = nullptr;
	b->prev = nullptr;
	mark(b, false);
	b->size = (g_end - g_start) | kMagic;
	g_head = b;
}

void* kmalloc(size_t size) {
	if (size == 0)
		size = 1;
	if (size > kSizeMax - sizeof(block))
		return nullptr;

	const uint32_t bytes = align_up((uint32_t)size + sizeof(block), kAlign);
	block* b = find_fit(bytes);
	if (!b)
		return nullptr;
	split(b, bytes);
	mark(b, true);
	++g_allocs;
	return (uint8_t*)b + sizeof(block);
}

void* kcalloc(size_t n, size_t size) {
	if (n && size > 0xFFFFFFFFu / n)
		return nullptr; // the multiply would wrap and ask for far too little
	const size_t total = n * size;
	void* p = kmalloc(total);
	if (p)
		memset(p, 0, total);
	return p;
}

void* krealloc(void* ptr, size_t size) {
	if (!ptr)
		return kmalloc(size);
	if (size == 0) {
		kfree(ptr);
		return nullptr;
	}
	if (size > kSizeMax - sizeof(block))
		return nullptr;
	block* b = (block*)((uint8_t*)ptr - sizeof(block));
	if (!ours(b) || !in_use(b))
		return nullptr; // not something we can grow
	const uint32_t want = align_up((uint32_t)size + sizeof(block), kAlign);

	if (!b->next) {
		const uint32_t room = g_end - align_down((uint32_t)b, kAlign);
		if (want <= room) {
			const uint32_t grown = want > bsize(b) ? want : bsize(b);
			b->size = grown | kMagic | kInUse;
			split(b, grown);
			mark(b, true);
			return ptr;
		}
	}
	void* fresh = kmalloc(size);
	if (!fresh)
		return nullptr;
	const uint32_t live = bsize(b) - sizeof(block);
	const uint32_t copy = size < live ? (uint32_t)size : live;
	memcpy(fresh, ptr, copy);
	kfree(ptr);
	return fresh;
}

void kfree(void* ptr) {
	if (!ptr)
		return;
	block* b = (block*)((uint8_t*)ptr - sizeof(block));
	if (!ours(b) || !in_use(b)) {
		++g_rejected;
		return;
	}
	mark(b, false);
	++g_frees;

	block* n = b->next;
	if (n && !in_use(n)) {
		b->size = (bsize(b) + bsize(n)) | kMagic;
		b->next = n->next;
		if (n->next)
			n->next->prev = b;
		++g_coalesced;
	}
	block* p = b->prev;
	if (p && !in_use(p)) {
		p->size = (bsize(p) + bsize(b)) | kMagic;
		p->next = b->next;
		if (b->next)
			b->next->prev = p;
		++g_coalesced;
	}
}

void* kframe_alloc_n(uint32_t pages) {
	if (pages == 0)
		return nullptr;
	const uint32_t want = pages * kPage + sizeof(block);
	for (block* b = g_head; b; b = b->next) {
		if (in_use(b))
			continue;
		uint8_t* payload = (uint8_t*)b + sizeof(block);
		if (((uint32_t)payload & (kPage - 1u)) != 0) {
			const uint32_t room = bsize(b);
			if (room < want + kPage)
				continue;
			const uint32_t cut = align_up((uint32_t)payload, kPage) - kAlign - (uint32_t)b;
			if (cut < kMinBlock || cut > room - want)
				continue;
			block* left = b;
			block* right = (block*)((uint8_t*)left + cut);
			block* after = left->next;
			right->size = (room - cut) | kMagic;
			right->prev_size = cut;
			mark(right, false);
			left->size = cut | kMagic;
			left->next = right;
			right->prev = left;
			if (after)
				after->prev = right;
			b = right;
			payload = (uint8_t*)b + sizeof(block);
		}
		if (bsize(b) < want)
			continue;
		split(b, want);
		mark(b, true);
		++g_allocs;
		g_frames += pages;
		return payload;
	}
	return nullptr;
}

void* kframe_alloc() { return kframe_alloc_n(1); }

void kframe_free(void* frame) { kfree(frame); }

size_t heap_largest_free() {
	size_t best = 0;
	for (block* b = g_head; b; b = b->next)
		if (!in_use(b) && bsize(b) - sizeof(block) > best)
			best = bsize(b) - sizeof(block);
	return best;
}

void heap_stats(size_t* total, size_t* used, size_t* largest_free) {
	size_t live = 0;
	for (block* b = g_head; b; b = b->next)
		if (in_use(b))
			live += bsize(b) - sizeof(block);
	if (total)
		*total = g_end - g_start;
	if (used)
		*used = live;
	if (largest_free)
		*largest_free = heap_largest_free();
}

void heap_counters(size_t* allocs, size_t* frees, size_t* coalesced, size_t* frames) {
	if (allocs)
		*allocs = g_allocs;
	if (frees)
		*frees = g_frees;
	if (coalesced)
		*coalesced = g_coalesced;
	if (frames)
		*frames = g_frames;
}
