#pragma once

// The engine is single-threaded from the host's point of view: all WKC API calls happen on the
// thread that runs this loop. Engine timer threads ask for wake-ups through WKCTimerEventHandler;
// those are queued here and delivered on the loop thread via WKCWebKitWakeUp.

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace nxb {

class RunLoop {
public:
    static RunLoop& main();

    // Thread-safe.
    void post_wakeup(void* timer, void* data);
    void cancel_wakeups(void* timer);
    void post(std::function<void()> task);

    // Runs until `done()` returns true or `timeout` passes. Returns done().
    bool run_until(const std::function<bool()>& done, std::chrono::milliseconds timeout);
    // Runs until no work arrives for `idle`, or `timeout` passes.
    void run_until_idle(std::chrono::milliseconds idle, std::chrono::milliseconds timeout);

private:
    struct Item {
        void* timer = nullptr;  // null for plain tasks
        void* data = nullptr;
        std::function<void()> task;
    };
    bool run_one(std::chrono::steady_clock::time_point until);

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Item> queue_;
};

}  // namespace nxb
