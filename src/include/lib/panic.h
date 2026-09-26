#pragma once

#include "arch/isr.h"

// fatal error
[[noreturn]] void panic(const char* msg);
[[noreturn]] void panic_regs(const char* why, Registers* r);