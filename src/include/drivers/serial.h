#pragma once

// com1, 115200 8n1. blocking io.
namespace serial {

void init();			  // poke the 16550 regs
void putc(char c);		  // waits till the tx buffer opens up
void puts(const char* s); // NUL-terminated

} // namespace serial