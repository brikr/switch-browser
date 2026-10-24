#include "host/run_loop.h"

#include <algorithm>

#include "host/wkc_api.h"
#include "log.h"

namespace nxb {

RunLoop& RunLoop::main() {
    static RunLoop loop;
    return loop;
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

// Waits for one item until `until`; runs it. Returns whether something ran.
bool RunLoop::run_one(std::chrono::steady_clock::time_point until) {
    Item item;
    {
        std::unique_lock<std::mutex> l(mutex_);
        if (!cv_.wait_until(l, until, [&] { return !queue_.empty(); })) return false;
        item = std::move(queue_.front());
        queue_.pop_front();
    }
    if (item.timer) {
        TRACE("wakeup timer=%p", item.timer);
        WKC::WKCWebKitWakeUp(item.timer, item.data);
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
    }
    return done();
}

void RunLoop::run_until_idle(std::chrono::milliseconds idle, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
        if (!run_one(std::min(deadline, std::chrono::steady_clock::now() + idle))) break;
}

}  // namespace nxb
