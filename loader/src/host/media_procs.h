#pragma once

// Media player callbacks (WKCWebKitSetMediaPlayerProcs). The engine's media peer refuses to
// initialise without them. The peers copy 0x1e0 bytes (core_0: 60 callbacks) / 0x228 bytes
// (core_3: 69); the only struct definition available is 3.30.3's 70-entry one, so this builds a
// raw table of the size the binary copies instead.
//   slot 0: fInitializeProc -> true; core_3: a null player whose loads fail with a format error;
// every other slot: a stub returning 0, reported through stub_report.h. No real playback yet.

namespace nxb {

constexpr int kMediaProcCount = NXB_CORE == 0 ? 60 : 69;
const void* media_player_procs();

}  // namespace nxb
