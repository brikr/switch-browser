#include "host/run_loop.h"

#include <algorithm>

#include "cfi_gate.h"
#include "host/wkc_api.h"
#include "log.h"

namespace nxb {

RunLoop& RunLoop::main() {
    static RunLoop loop;
    return loop;
}

#if NXB_CORE == 3
static void engine_wakeup() {
    RunLoop::main().wake();
}
#endif

void RunLoop::attach_engine() {
#if NXB_CORE == 3
    WKC::WKCWebKitRunLoopSetWakeUpCallback(gate_function(&engine_wakeup));
#endif
}

void RunLoop::post_wakeup(void* timer, void* data) {
    {
        std::lock_guard<std::mutex> g(mutex_);
        queue_.push_back(Item{timer, data, {}});
    }
    cv_.notify_one();
}

void RunLoop::cancel_wakeups(void* timer) {
    std::lock_guard<std::mutex> g(mutex_);
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [&](const Item& i) { return i.timer == timer; }),
                 queue_.end());
}

void RunLoop::post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> g(mutex_);
        queue_.push_back(Item{nullptr, nullptr, std::move(task)});
    }
    cv_.notify_one();
}

void RunLoop::wake() {
    {
        std::lock_guard<std::mutex> g(mutex_);
        woken_ = true;
    }
    cv_.notify_one();
}

bool RunLoop::run_one(std::chrono::steady_clock::time_point until) {
#if NXB_CORE == 3
    WKC::WKCWebKitIterateRunLoop();
    // Engine work may be due again soon; never sleep past it (or 100 ms, as a safety net).
    uint64_t next_us = WKC::WKCWebKitRunLoopGetNextWakeUpTime();
    auto engine_due = std::chrono::steady_clock::now() + std::chrono::microseconds(std::min<uint64_t>(next_us, 100000));
    until = std::min(until, engine_due);
#endif
    Item item;
    {
        std::unique_lock<std::mutex> l(mutex_);
        bool ready = cv_.wait_until(l, until, [&] { return !queue_.empty() || woken_; });
#if NXB_CORE == 3
        bool engine_timer_fired = !ready && std::chrono::steady_clock::now() >= engine_due && next_us <= 100000;
        if (!ready) return engine_timer_fired;
        woken_ = false;
        if (queue_.empty()) return true;
#else
        if (!ready) return false;
        woken_ = false;
        if (queue_.empty()) return true;  // woken without a queued item
#endif
        item = std::move(queue_.front());
        queue_.pop_front();
    }
    if (item.timer) {
#if NXB_CORE == 0
        TRACE("wakeup timer=%p", item.timer);
        WKC::WKCWebKitWakeUp(item.timer, item.data);
#endif
    } else {
        item.task();
    }
    return true;
}

bool RunLoop::run_until(const std::function<bool()>& done, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        run_one(std::min(deadline, now + std::chrono::milliseconds(50)));
        if (on_iteration) on_iteration();
    }
    return done();
}

void RunLoop::run_until_idle(std::chrono::milliseconds idle, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
        if (!run_one(std::min(deadline, std::chrono::steady_clock::now() + idle))) break;
}

}  // namespace nxb
