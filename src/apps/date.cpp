#include "lib.h"

#include "lib/datetime.h"

#include <stdint.h>

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	static const char* const kDays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static const char* const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
											"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	const datetime::parts p = datetime::from_unix((uint32_t)time());
	printf("%s %s %2u %02u:%02u:%02u UTC %d\n", kDays[p.weekday], kMonths[p.month - 1], p.day, p.hour, p.minute,
		   p.second, p.year);
	return 0;
}
