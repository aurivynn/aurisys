#pragma once

#include <stdint.h>

namespace time {

uint32_t ms(); // since boot, from the irq0 tick

bool init();
uint32_t unix(); // UTC seconds since 1970

} // namespace time
