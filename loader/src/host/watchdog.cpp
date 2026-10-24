#include "host/watchdog.h"

#include <pthread.h>
#include <signal.h>
#include <ucontext.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include "linker.h"
#include "log.h"

namespace nxb {
namespace {

constexpr int kSampleSignal = SIGPROF;
std::atomic<int64_t> g_last_beat{0};

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Engine return addresses carry the nncfi hash in their top bits; user addresses fit in 40 bits.
uintptr_t strip(uint64_t a) { return static_cast<uintptr_t>(a & 0xffffffffffULL); }

// Not async-signal-safe (stdio, describe()), which is acceptable for a debugging aid.
void sample_handler(int, siginfo_t*, void* ctx) {
    auto* uc = static_cast<ucontext_t*>(ctx);
    const auto& mc = uc->uc_mcontext;
    Linker& linker = Linker::instance();
    LOG("watchdog: loop thread busy for %lld ms", static_cast<long long>(now_ms() - g_last_beat.load()));
    LOG("  pc %s", linker.describe(strip(mc.pc)).c_str());
    LOG("  lr %s", linker.describe(strip(mc.regs[30])).c_str());
    auto* fp = reinterpret_cast<const uint64_t*>(mc.regs[29]);
    for (int i = 0; fp && i < 48; i++) {
        uintptr_t ret = strip(fp[1]);
        if (!ret) break;
        LOG("  #%-2d %s", i, linker.describe(ret).c_str());
        auto* next = reinterpret_cast<const uint64_t*>(fp[0]);
        if (next <= fp) break;  // frames grow upwards; stop on a broken chain
        fp = next;
    }
}

}  // namespace

void watchdog_heartbeat() { g_last_beat.store(now_ms(), std::memory_order_relaxed); }

void start_watchdog() {
    const char* env = std::getenv("NXB_WATCHDOG");
    int period_s = env ? std::atoi(env) : 0;
    if (period_s <= 0) return;
    struct sigaction sa {};
    sa.sa_sigaction = sample_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(kSampleSignal, &sa, nullptr);
    watchdog_heartbeat();
    pthread_t loop_thread = pthread_self();
    std::thread([loop_thread, period_s] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(period_s));
            if (now_ms() - g_last_beat.load(std::memory_order_relaxed) >= period_s * 1000LL)
                pthread_kill(loop_thread, kSampleSignal);
        }
    }).detach();
    LOG("watchdog: sampling the loop thread when an iteration exceeds %d s", period_s);
}

}  // namespace nxb
