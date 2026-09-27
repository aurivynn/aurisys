#pragma once

namespace procfs {

// hang /proc on the tree. it is computed from the process table on every read, so nothing has to be kept in step when a
// process comes or goes
void init();

} // namespace procfs
