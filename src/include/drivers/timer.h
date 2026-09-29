#pragma once

#include <stdint.h>

void timer_init();

// how many ticks are as many milliseconds, rounded to nearest.
uint32_t ticks_for_ms(uint32_t ms);
