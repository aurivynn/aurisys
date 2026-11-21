#pragma once

#include <stdint.h>

namespace datetime {

struct parts {
	int year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t weekday;
};

// days since 1970-01-01
inline uint32_t days_from_civil(int y, unsigned m, unsigned d) {
	y -= m <= 2;
	const uint32_t era = (uint32_t)y / 400u;
	const uint32_t yoe = (uint32_t)y - era * 400u;
	const uint32_t doy = (153u * (m > 2 ? m - 3 : m + 9) + 2u) / 5u + d - 1u;
	const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
	return era * 146097u + doe - 719468u;
}

inline parts from_unix(uint32_t t) {
	parts p = {};
	const uint32_t days = t / 86400u;
	const uint32_t sod = t % 86400u;
	p.hour = (uint8_t)(sod / 3600u);
	p.minute = (uint8_t)((sod / 60u) % 60u);
	p.second = (uint8_t)(sod % 60u);
	p.weekday = (uint8_t)((days + 4u) % 7u); // 1970-01-01 was a Thursday

	const uint32_t z = days + 719468u;
	const uint32_t era = z / 146097u;
	const uint32_t doe = z - era * 146097u;
	const uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
	int y = (int)(yoe + era * 400u);
	const uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
	const uint32_t mp = (5u * doy + 2u) / 153u;
	p.day = (uint8_t)(doy - (153u * mp + 2u) / 5u + 1u);
	p.month = (uint8_t)(mp < 10u ? mp + 3u : mp - 9u);
	y += (p.month <= 2u);
	p.year = y;
	return p;
}

} // namespace datetime
