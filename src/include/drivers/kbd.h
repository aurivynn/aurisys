#pragma once

// ps/2 keyboard. irq1 drains scancodes into a ring buffer, poll() pops them.
namespace kbd {

void init(); // installs the irq1 handler + unmask
int poll();	 // next ascii char, or -1 if none

} // namespace kbd