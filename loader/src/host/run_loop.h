#pragma once

// The engine is single-threaded from the host's point of view: all WKC API calls happen on the
// thread that runs this loop.
//  - core_0: engine timer threads ask for wake-ups through WKCTimerEventHandler; those are queued
//    and delivered here via WKCWebKitWakeUp.
//  - core_3: the engine has its own run loop; we call WKCWebKitIterateRunLoop and sleep until its
//    next wake-up time or its wake-up callback fires.

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace nxb {

class RunLoop {
public:
    static RunLoop& main();

    // core_3: registers the engine wake-up callback. Call after WKCWebKitInitialize.
    void attach_engine();

    // Thread-safe.
    void post_wakeup(void* timer, void* data);  // core_0
    void cancel_wakeups(void* timer);           // core_0
    void post(std::function<void()> task);
    void wake();

    // Runs until `done()` returns true or `timeout` passes. Returns done().
    bool run_until(const std::function<bool()>& done, std::chrono::milliseconds timeout);
    // Runs until nothing happens for `idle`, or `timeout` passes.
    void run_until_idle(std::chrono::milliseconds idle, std::chrono::milliseconds timeout);

private:
    struct Item {
        void* timer = nullptr;  // null for plain tasks
        void* data = nullptr;
        std::function<void()> task;
    };
    // Does one round of work, waiting at most until `until` for something to happen.
    // Returns whether anything happened before `until`.
    bool run_one(std::chrono::steady_clock::time_point until);

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Item> queue_;
    bool woken_ = false;
};

}  // namespace nxb
