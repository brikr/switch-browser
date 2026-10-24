#pragma once

namespace nxb {

// Debugging aid, enabled with NXB_WATCHDOG=<seconds>: if the loop thread stays inside one run-loop
// iteration for longer than that (a script or engine task that never returns), it is interrupted
// with a signal and its pc and frame-pointer chain are printed as module+offset, once per period.
// (qemu's gdb stub can't interrupt such loops.)
void start_watchdog();
// Called by the run loop at the start of every iteration.
void watchdog_heartbeat();

}  // namespace nxb
