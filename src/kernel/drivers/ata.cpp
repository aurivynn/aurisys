#include "drivers/ata.h"

#include <stdint.h>

namespace {

inline void outb(uint16_t port, uint8_t val) { asm volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }
inline uint8_t inb(uint16_t port) {
	uint8_t val;
	asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}
inline void outw(uint16_t port, uint16_t val) { asm volatile("outw %0, %1" : : "a"(val), "Nd"(port)); }
inline uint16_t inw(uint16_t port) {
	uint16_t val;
	asm volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

constexpr uint16_t kBase = 0x1F0; // primary controller
constexpr uint8_t kData = 0;	  // 16bit data port
constexpr uint8_t kSectorCount = 2;
constexpr uint8_t kLbaLow = 3;
constexpr uint8_t kLbaMid = 4;
constexpr uint8_t kLbaHigh = 5;
constexpr uint8_t kDriveHead = 6;
constexpr uint8_t kCommand = 7; // out
constexpr uint8_t kStatus = 7;	// in

constexpr uint8_t kStBsy = 0x80;
constexpr uint8_t kStDrq = 0x08; // data transfer requested
constexpr uint8_t kStErr = 0x01;

constexpr uint32_t kTimeout = 30000000;

// wait until the drive is not busy
bool wait_idle() {
	for (uint32_t i = 0; i < kTimeout; ++i) {
		const uint8_t st = inb(kBase + kStatus);
		if (st & kStErr)
			return false;
		if (!(st & kStBsy))
			return true;
	}
	return false;
}

// wait until data requested
bool wait_drq() {
	for (uint32_t i = 0; i < kTimeout; ++i) {
		const uint8_t st = inb(kBase + kStatus);
		if ((st & (kStErr | kStBsy)) == kStErr)
			return false;
		if (st & kStDrq)
			return true;
		if (!(st & kStBsy)) // finished without wanting data
			return false;
	}
	return false;
}

void write_lba(uint32_t lba, uint8_t count) {
	outb(kBase + kSectorCount, count);
	outb(kBase + kLbaLow, (uint8_t)lba);
	outb(kBase + kLbaMid, (uint8_t)(lba >> 8));
	outb(kBase + kLbaHigh, (uint8_t)(lba >> 16));
	outb(kBase + kDriveHead, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F))); // lba mode + master
}

// read the data register into a byte buffer 256 words per sector
void read_data(void* dst, int words) {
	uint16_t* p = (uint16_t*)dst;
	for (int i = 0; i < words; ++i)
		p[i] = inw(kBase + kData);
}

void write_data(const void* src, int words) {
	const uint16_t* p = (const uint16_t*)src;
	for (int i = 0; i < words; ++i)
		outw(kBase + kData, p[i]);
}

} // namespace

namespace ata {

uint32_t sector_count;

bool init() {
	outb(kBase + kDriveHead, 0xA0); // select master, no lba yet
	if (!wait_idle())
		return false;

	outb(kBase + kCommand, 0xEC); // identify
	if (!wait_drq())
		return false;

	uint16_t id[256];
	read_data(id, 256);
	if (id[0] == 0x0000)
		return false; // no valid identify payload
	if (id[0] == 0x9669 || id[0] == 0xEB14)
		return false; // atapi packet drive

	sector_count = ((uint32_t)id[61] << 16) | id[60];

	constexpr uint32_t kLba28Max = 0x0FFFFFFF;
	if (sector_count > kLba28Max)
		sector_count = kLba28Max;
	return true;
}

bool read_sector(uint32_t lba, void* dst) {
	if (lba >= sector_count)
		return false;
	write_lba(lba, 1);
	outb(kBase + kCommand, 0x20); // read sectors
	if (!wait_drq())
		return false;
	read_data(dst, 256);
	return wait_idle();
}

bool write_sector(uint32_t lba, const void* src) {
	if (lba >= sector_count)
		return false;
	write_lba(lba, 1);
	outb(kBase + kCommand, 0x30); // write sectors
	if (!wait_drq())
		return false;
	write_data(src, 256);
	return wait_idle();
}

bool read_sectors(uint32_t lba, uint32_t n, void* dst) {
	uint8_t* p = (uint8_t*)dst;
	for (uint32_t i = 0; i < n; ++i) {
		if (!read_sector(lba + i, p))
			return false;
		p += 512;
	}
	return true;
}

bool write_sectors(uint32_t lba, uint32_t n, const void* src) {
	const uint8_t* p = (const uint8_t*)src;
	for (uint32_t i = 0; i < n; ++i) {
		if (!write_sector(lba + i, p))
			return false;
		p += 512;
	}
	return true;
}

} // namespace ata