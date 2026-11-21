#include "drivers/rtc.h"

#include "lib/datetime.h"

#include <stdint.h>

namespace {

inline void outb(uint16_t port, uint8_t val) { asm volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }
inline uint8_t inb(uint16_t port) {
	uint8_t val;
	asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

uint8_t cmos(uint8_t reg) {
	outb(0x70, reg);
	return inb(0x71);
}

bool settled() {
	for (int i = 0; i < 100000; ++i)
		if (!(cmos(0x0A) & 0x80))
			return true;
	return false;
}

uint8_t bcd(uint8_t v) { return (uint8_t)((v & 0x0F) + (v >> 4) * 10); }

} // namespace

namespace rtc {

bool read(uint32_t* out) {
	if (!out)
		return false;

	uint8_t sec = 0, min = 0, hour = 0, day = 0, mon = 0, year = 0, cent = 0, status = 0;
	bool got = false;

	for (int tries = 0; tries < 6 && !got; ++tries) {
		if (!settled())
			return false;
		const uint8_t s0 = cmos(0x00);
		status = cmos(0x0B);
		min = cmos(0x02);
		hour = cmos(0x04);
		day = cmos(0x07);
		mon = cmos(0x08);
		year = cmos(0x09);
		cent = cmos(0x32);
		if (!settled())
			return false;
		const uint8_t s1 = cmos(0x00);
		if (s0 == s1) {
			sec = s0;
			got = true;
		}
	}
	if (!got)
		return false;

	const bool binary = (status & 0x04) != 0;
	const bool h24 = (status & 0x02) != 0;
	auto dec = [binary](uint8_t v) { return binary ? (unsigned)v : (unsigned)bcd(v); };

	unsigned second = dec(sec);
	unsigned minute = dec(min);
	const bool pm = !h24 && (hour & 0x80);
	const unsigned h = h24 ? dec(hour) : dec((uint8_t)(hour & 0x7F));
	unsigned hours = (h == 12 && !h24) ? 0 : h;
	if (pm)
		hours += 12;

	unsigned d = dec(day);
	unsigned m = dec(mon);
	unsigned y = dec(year);
	const unsigned c = dec(cent);
	if (c >= 19 && c <= 21)
		y = c * 100u + y;
	else if (y < 70)
		y += 2000;
	else
		y += 1900;

	static const uint8_t kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (m < 1 || m > 12 || y < 1970 || y > 2099 || hours > 23 || minute > 59 || second > 59)
		return false;
	const bool leap = (y % 4u == 0 && (y % 100u != 0 || y % 400u == 0));
	const unsigned mdays = kMonthDays[m - 1] + ((m == 2 && leap) ? 1u : 0u);
	if (d < 1 || d > mdays)
		return false;

	const int64_t days = datetime::days_from_civil((int)y, m, d);
	if (days < 0)
		return false;
	const uint64_t t = (uint64_t)days * 86400u + hours * 3600u + minute * 60u + second;
	if (t > 0xFFFFFFFFu)
		return false;

	*out = (uint32_t)t;
	return true;
}

} // namespace rtc
