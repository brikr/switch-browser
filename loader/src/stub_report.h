#pragma once

// Visibility into host features that aren't implemented: generated client stubs, unimplemented
// media-player procs and partial shims all report here. A stub returns a default (0 / false /
// empty), and the engine usually carries on as if the feature didn't exist, so this is how a page
// that depends on a missing feature shows up.
//  - Unexpected stubs are logged as "[stub] <name>" on their first call.
//  - Expected ones (no-op is correct for this host; NOOP_OK in gen_client_stubs.py) are only
//    counted, unless NXB_STUBS=all.
//  - NXB_TRACE=1 also logs every later call.

#include <functional>
#include <string>
#include <vector>

namespace nxb {

// Thread-safe; `name` must be a string literal or otherwise outlive the process.
void report_stub(const char* name, bool expected = false);

// Called the first time each unexpected stub is hit, on the calling thread (which may not be the
// loop thread).
void set_stub_listener(std::function<void(const char* name)> listener);

struct StubCount {
    std::string name;
    unsigned long calls;
    bool expected;
};
// Every stub hit so far, unexpected ones first, then by call count.
std::vector<StubCount> stub_counts();
// Logs the unexpected stubs (all of them with NXB_STUBS=all) with call counts.
void log_stub_summary();

}  // namespace nxb
