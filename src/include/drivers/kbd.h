#pragma once

// ps/2 keyboard. irq1 drains scancodes into a ring buffer, poll() pops them.
namespace kbd {

enum Key : int {
	KEY_UP = 0x101,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_HOME,
	KEY_END,
	KEY_PGUP,
	KEY_PGDN,
	KEY_DEL,
	KEY_INS,
};

void init(); // installs the irq1 handler + unmask
int poll();	 // next key: ascii, a Key, or -1 if nothing queued

void wait();
void wake(); // a key arrived, or one is about to

int poll_char();

} // namespace kbd