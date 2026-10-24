#include "host/media_procs.h"

#include <cstdint>
#include <mutex>
#include <set>
#include <utility>

#include "cfi_gate.h"
#include "host/run_loop.h"
#include "log.h"
#include "stub_report.h"

namespace nxb {
namespace {

bool media_initialize() { return true; }

#if NXB_CORE == 3
// A "null player": players can be created, but every load ends in a format error (the state an
// unsupported file gives), so <video>/<audio> fire their error events instead of the engine
// crashing on a null player (MediaPlayerPrivateWKC treats that as out of memory). The procs are
// still reported as stubs. Slot numbers and constants are from 3.30.3's wkcmediaprocs.h and
// wkcmediapeer.h.
constexpr int kCreate = 8, kDelete = 9, kLoad = 10, kNetworkState = 37;
constexpr int kNetworkStateEmpty = 0, kNetworkStateFormatError = 4;  // WKC_MEDIA_NETWORKSTATE_*
constexpr int kPlayerStateNetworkStateChanged = 0;                   // WKC_MEDIA_PLAYERSTATE_*

struct NullPlayer {
    void (*notify_player_state)(void* opaque, int state);  // WKCMediaPlayerCallbacks::fNotifyPlayerState
    void* opaque;
    bool loaded = false;
};
std::mutex g_players_mutex;
std::set<NullPlayer*> g_players;  // live players, so a queued notification can't outlive its player

bool is_live(NullPlayer* p) {
    std::lock_guard<std::mutex> l(g_players_mutex);
    return g_players.count(p) != 0;
}

void* media_create(void* const* callbacks, void* opaque) {
    report_stub("MediaPlayerProcs::fCreateProc");
    auto* p = new NullPlayer{reinterpret_cast<void (*)(void*, int)>(callbacks[0]), opaque};
    std::lock_guard<std::mutex> l(g_players_mutex);
    g_players.insert(p);
    return p;
}

void media_delete(void* self) {
    auto* p = static_cast<NullPlayer*>(self);
    {
        std::lock_guard<std::mutex> l(g_players_mutex);
        g_players.erase(p);
    }
    delete p;
}

int media_load(void* self, const char* uri, int) {
    report_stub("MediaPlayerProcs::fLoadProc");
    TRACE("media load %s -> format error", uri ? uri : "(null)");
    auto* p = static_cast<NullPlayer*>(self);
    p->loaded = true;
    // Notify after the load call returns, as a real player would.
    RunLoop::main().post([p] {
        if (is_live(p) && p->notify_player_state) p->notify_player_state(p->opaque, kPlayerStateNetworkStateChanged);
    });
    RunLoop::main().wake();
    return 0;  // WKC_MEDIA_ERROR_OK
}

int media_network_state(void* self) {
    return static_cast<NullPlayer*>(self)->loaded ? kNetworkStateFormatError : kNetworkStateEmpty;
}
#endif

#if NXB_CORE == 3
// WKCMediaPlayerProcs field order (3.30.3 wkcmediaprocs.h, which matches core_3's 69 entries).
const char* const kProcNames[kMediaProcCount] = {
    "MediaPlayerProcs::fInitializeProc", "MediaPlayerProcs::fFinalizeProc", "MediaPlayerProcs::fForceTerminateProc",
    "MediaPlayerProcs::fIsSupportedMIMETypeProc", "MediaPlayerProcs::fSupportedMIMETypesProc",
    "MediaPlayerProcs::fSupportedMIMETypeProc", "MediaPlayerProcs::fGetUIStringProc",
    "MediaPlayerProcs::fGetUIStringTimeProc", "MediaPlayerProcs::fCreateProc", "MediaPlayerProcs::fDeleteProc",
    "MediaPlayerProcs::fLoadProc", "MediaPlayerProcs::fCancelLoadProc", "MediaPlayerProcs::fPrepareToPlayProc",
    "MediaPlayerProcs::fPlayProc", "MediaPlayerProcs::fPauseProc", "MediaPlayerProcs::fIsPausedProc",
    "MediaPlayerProcs::fHasVideoProc", "MediaPlayerProcs::fHasAudioProc",
    "MediaPlayerProcs::fSupportsFullScreenProc", "MediaPlayerProcs::fCanSaveMediaDataProc",
    "MediaPlayerProcs::fHasClosedCaptionsProc", "MediaPlayerProcs::fSetClosedCaptionsVisibleProc",
    "MediaPlayerProcs::fHasSingleSecurityOriginProc", "MediaPlayerProcs::fNaturalSizeProc",
    "MediaPlayerProcs::fSetSizeProc", "MediaPlayerProcs::fSetPreloadProc",
    "MediaPlayerProcs::fSetPageIsVisibleProc", "MediaPlayerProcs::fDurationProc",
    "MediaPlayerProcs::fStartTimeProc", "MediaPlayerProcs::fCurrentTimeProc",
    "MediaPlayerProcs::fMaxTimeSeekableProc", "MediaPlayerProcs::fSeekProc", "MediaPlayerProcs::fRateProc",
    "MediaPlayerProcs::fSetRateProc", "MediaPlayerProcs::fSetPreservesPitchProc", "MediaPlayerProcs::fVolumeProc",
    "MediaPlayerProcs::fSetVolumeProc", "MediaPlayerProcs::fNetworkStateProc", "MediaPlayerProcs::fReadyStateProc",
    "MediaPlayerProcs::fMovieLoadTypeProc", "MediaPlayerProcs::fSetWindowPositionProc",
    "MediaPlayerProcs::fVideoSinkTypeProc", "MediaPlayerProcs::fBufferedRangesProc",
    "MediaPlayerProcs::fBufferedRangeProc", "MediaPlayerProcs::fSetLoopProc",
    "MediaPlayerProcs::fRemovedFromDocumentProc", "MediaPlayerProcs::fWillBecomeInactiveProc",
    "MediaPlayerProcs::fEnterFullscreenProc", "MediaPlayerProcs::fExitFullscreenProc",
    "MediaPlayerProcs::fCanEnterFullscreenProc", "MediaPlayerProcs::fVideoPlaybackQualityMetricsProc",
    "MediaPlayerProcs::fDecodeAudioDataProc", "MediaPlayerProcs::fAddSBProc",
    "MediaPlayerProcs::fGetMSDurationProc", "MediaPlayerProcs::fSetMSDurationProc",
    "MediaPlayerProcs::fNotifyMSMarkEndOfStreamProc", "MediaPlayerProcs::fNotifyMSUnmarkEndOfStreamProc",
    "MediaPlayerProcs::fMSIsEndedProc", "MediaPlayerProcs::fSetReadyStateProc",
    "MediaPlayerProcs::fSetActiveSBProc", "MediaPlayerProcs::fAppendToSBProc", "MediaPlayerProcs::fAbortSBProc",
    "MediaPlayerProcs::fSetSBTimestampOffsetProc", "MediaPlayerProcs::fRemoveSBProc",
    "MediaPlayerProcs::fRemoveCodedFramesProc", "MediaPlayerProcs::fEvictCodedFramesSBProc",
    "MediaPlayerProcs::fIsFullSBProc", "MediaPlayerProcs::fCanSwitchToTypeProc",
    "MediaPlayerProcs::fSetCurlVerboseProc",
};
#else
// core_0's 60-entry layout isn't known by name; report by index.
#define P(n) "MediaPlayerProcs #" #n
const char* const kProcNames[kMediaProcCount] = {
    P(0),  P(1),  P(2),  P(3),  P(4),  P(5),  P(6),  P(7),  P(8),  P(9),  P(10), P(11), P(12), P(13), P(14),
    P(15), P(16), P(17), P(18), P(19), P(20), P(21), P(22), P(23), P(24), P(25), P(26), P(27), P(28), P(29),
    P(30), P(31), P(32), P(33), P(34), P(35), P(36), P(37), P(38), P(39), P(40), P(41), P(42), P(43), P(44),
    P(45), P(46), P(47), P(48), P(49), P(50), P(51), P(52), P(53), P(54), P(55), P(56), P(57), P(58), P(59),
};
#undef P
#endif

template <int N>
uintptr_t media_stub() {
    report_stub(kProcNames[N]);
    return 0;
}

template <int... N>
constexpr auto make_table(std::integer_sequence<int, N...>) {
    struct Table {
        const void* procs[sizeof...(N)];
    };
    return Table{{reinterpret_cast<const void*>(&media_stub<N>)...}};
}

}  // namespace

const void* media_player_procs() {
    static auto table = [] {
        auto t = make_table(std::make_integer_sequence<int, kMediaProcCount>());
        t.procs[0] = reinterpret_cast<const void*>(&media_initialize);
#if NXB_CORE == 3
        t.procs[kCreate] = reinterpret_cast<const void*>(&media_create);
        t.procs[kDelete] = reinterpret_cast<const void*>(&media_delete);
        t.procs[kLoad] = reinterpret_cast<const void*>(&media_load);
        t.procs[kNetworkState] = reinterpret_cast<const void*>(&media_network_state);
#endif
        for (auto& proc : t.procs) proc = reinterpret_cast<const void*>(make_gate(proc, GateKind::Full));
        return t;
    }();
    return table.procs;
}

}  // namespace nxb
