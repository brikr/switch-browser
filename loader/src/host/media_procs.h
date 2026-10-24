#pragma once

// Media player callbacks (WKCWebKitSetMediaPlayerProcs). The engine's media peer refuses to
// initialise without them. core_0's peer copies 0x1e0 bytes (60 callbacks); the only struct
// definition available is 3.30.3's 70-entry one, so this builds a raw 60-slot table instead.
//   slot 0: fInitializeProc -> true; every other slot: a stub returning 0 that logs its index
// (with NXB_TRACE=1), until real media support exists.

namespace nxb {

constexpr int kMediaProcCount = 60;
const void* media_player_procs();

}  // namespace nxb
