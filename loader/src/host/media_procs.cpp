#include "host/media_procs.h"

#include <cstdint>
#include <utility>

#include "cfi_gate.h"
#include "log.h"

namespace nxb {
namespace {

bool media_initialize() { return true; }

template <int N>
uintptr_t media_stub() {
    TRACE("media player proc #%d called (unimplemented)", N);
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
        for (auto& proc : t.procs) proc = reinterpret_cast<const void*>(make_gate(proc, GateKind::Full));
        return t;
    }();
    return table.procs;
}

}  // namespace nxb
