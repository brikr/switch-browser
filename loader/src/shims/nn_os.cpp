// nn::os stand-ins: threads, mutexes, events, timer events, ticks and memory pages, on pthreads.
//
// The engine allocates the SDK's ThreadType/MutexType/EventType/TimerEventType structs itself
// (layouts are Nintendo's). We never touch their contents; host state lives in side tables keyed
// by the struct's address.

#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "../host_symbols.h"
#include "../log.h"
#include "sync_table.h"

namespace {

using Result = uint32_t;
constexpr Result kSuccess = 0;

// Passed by value as one 64-bit register, like the SDK types.
struct TimeSpan { int64_t ns; };
struct Tick { int64_t value; };

constexpr int64_t kTickFrequency = 19'200'000;  // Switch system counter

int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::chrono::steady_clock::time_point deadline_after(int64_t ns) {
    return std::chrono::steady_clock::now() + std::chrono::nanoseconds(ns < 0 ? 0 : ns);
}

// --- threads ---

struct HostThread {
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
    size_t stack_size = 0;
    int priority = 16;
    std::string name;
    pthread_t pthread{};
    bool started = false;
    bool joined = false;
};

SyncTable<HostThread> g_threads;
thread_local void* t_current_thread = nullptr;  // engine ThreadType* for this host thread

void* thread_trampoline(void* p) {
    void* thread_type = p;
    t_current_thread = thread_type;
    HostThread& t = g_threads.get(thread_type);
    TRACE("thread %s starting", t.name.c_str());
    t.entry(t.arg);
    return nullptr;
}

Result CreateThread(void* thread, void (*entry)(void*), void* arg, void* /*stack*/, size_t stack_size, int priority,
                    int /*ideal_core*/) {
    auto* t = new HostThread();
    t->entry = entry;
    t->arg = arg;
    t->stack_size = stack_size;
    t->priority = priority;
    t->name = "Thread";
    g_threads.reset(thread, t);
    TRACE("CreateThread %p entry=%p stack=%#zx prio=%d", thread, (void*)entry, stack_size, priority);
    return kSuccess;
}

void StartThread(void* thread) {
    HostThread& t = g_threads.get(thread);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // The engine supplies its own stack; we use a host-allocated one of at least that size, with
    // headroom for host shim frames.
    pthread_attr_setstacksize(&attr, std::max<size_t>(t.stack_size, 0x40000) + 0x40000);
    if (pthread_create(&t.pthread, &attr, thread_trampoline, thread) != 0) FATAL("pthread_create failed");
    pthread_attr_destroy(&attr);
    t.started = true;
}

void WaitThread(void* thread) {
    HostThread& t = g_threads.get(thread);
    if (t.started && !t.joined) {
        pthread_join(t.pthread, nullptr);
        t.joined = true;
    }
}

void DestroyThread(void* thread) {
    WaitThread(thread);
    g_threads.erase(thread);
}

void* GetCurrentThread() {
    if (!t_current_thread) {
        // A thread the engine didn't create (e.g. the host main thread): give it a ThreadType.
        void* fake = std::calloc(1, 0x200);
        HostThread& t = g_threads.get(fake);
        t.name = "MainThread";
        t.pthread = pthread_self();
        t.started = true;
        t_current_thread = fake;
    }
    return t_current_thread;
}

void SetThreadName(void* thread, const char* name) { g_threads.get(thread).name = name ? name : ""; }
const char* GetThreadNamePointer(const void* thread) { return g_threads.get(thread).name.c_str(); }
int GetThreadPriority(const void* thread) { return g_threads.get(thread).priority; }
int GetCurrentCoreNumber() {
    int cpu = sched_getcpu();
    return cpu < 0 ? 0 : cpu % 3;
}
uint64_t GetThreadAvailableCoreMask() { return 0b111; }
void SleepThread(TimeSpan t) { std::this_thread::sleep_for(std::chrono::nanoseconds(t.ns)); }
void YieldThread() { sched_yield(); }

// --- mutexes (optionally recursive) ---

struct HostOsMutex {
    std::recursive_mutex m;
    bool recursive = false;
    std::atomic<std::thread::id> owner{};
    int depth = 0;
};
SyncTable<HostOsMutex> g_os_mutexes;

void InitializeMutex(void* mutex, bool recursive, int /*lock_level*/) {
    auto* m = new HostOsMutex();
    m->recursive = recursive;
    g_os_mutexes.reset(mutex, m);
}
void FinalizeMutex(void* mutex) { g_os_mutexes.erase(mutex); }

void LockMutex(void* mutex) {
    HostOsMutex& m = g_os_mutexes.get(mutex);
    if (!m.recursive && m.owner.load() == std::this_thread::get_id()) FATAL("recursive lock of non-recursive nn::os mutex %p", mutex);
    m.m.lock();
    m.owner = std::this_thread::get_id();
    m.depth++;
}
bool TryLockMutex(void* mutex) {
    HostOsMutex& m = g_os_mutexes.get(mutex);
    if (!m.recursive && m.owner.load() == std::this_thread::get_id()) return false;
    if (!m.m.try_lock()) return false;
    m.owner = std::this_thread::get_id();
    m.depth++;
    return true;
}
void UnlockMutex(void* mutex) {
    HostOsMutex& m = g_os_mutexes.get(mutex);
    if (--m.depth == 0) m.owner = std::thread::id();
    m.m.unlock();
}
bool IsMutexLockedByCurrentThread(const void* mutex) {
    return g_os_mutexes.get(mutex).owner.load() == std::this_thread::get_id();
}

// --- events and timer events ---

enum EventClearMode { kManualClear = 0, kAutoClear = 1 };

struct HostEvent {
    std::mutex m;
    std::condition_variable cv;
    bool signaled = false;
    int clear_mode = kAutoClear;
    // Timer events only: when the one-shot timer fires.
    bool timer_armed = false;
    std::chrono::steady_clock::time_point timer_deadline;

    // Called with m held. Fires an expired timer.
    void poll_timer() {
        if (timer_armed && std::chrono::steady_clock::now() >= timer_deadline) {
            timer_armed = false;
            signaled = true;
        }
    }
    // Called with m held, once a wait succeeds.
    void consume() {
        if (clear_mode == kAutoClear) signaled = false;
    }
    void signal() {
        std::lock_guard<std::mutex> g(m);
        signaled = true;
        cv.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> l(m);
        for (;;) {
            poll_timer();
            if (signaled) break;
            if (timer_armed) cv.wait_until(l, timer_deadline);
            else cv.wait(l);
        }
        consume();
    }
    bool wait_for(int64_t ns) {
        auto deadline = deadline_after(ns);
        std::unique_lock<std::mutex> l(m);
        for (;;) {
            poll_timer();
            if (signaled) break;
            auto until = timer_armed ? std::min(deadline, timer_deadline) : deadline;
            if (cv.wait_until(l, until) == std::cv_status::timeout && std::chrono::steady_clock::now() >= deadline) {
                poll_timer();
                if (!signaled) return false;
                break;
            }
        }
        consume();
        return true;
    }
};
SyncTable<HostEvent> g_events;

void InitializeEvent(void* event, bool signaled, int clear_mode) {
    auto* e = new HostEvent();
    e->signaled = signaled;
    e->clear_mode = clear_mode;
    g_events.reset(event, e);
}
void FinalizeEvent(void* event) { g_events.erase(event); }
void SignalEvent(void* event) { g_events.get(event).signal(); }
void WaitEvent(void* event) { g_events.get(event).wait(); }
bool TimedWaitEvent(void* event, TimeSpan timeout) { return g_events.get(event).wait_for(timeout.ns); }

void InitializeTimerEvent(void* timer, int clear_mode) {
    auto* e = new HostEvent();
    e->clear_mode = clear_mode;
    g_events.reset(timer, e);
}
void FinalizeTimerEvent(void* timer) { g_events.erase(timer); }
void SignalTimerEvent(void* timer) { g_events.get(timer).signal(); }
void WaitTimerEvent(void* timer) { g_events.get(timer).wait(); }
void StartOneShotTimerEvent(void* timer, TimeSpan after) {
    HostEvent& e = g_events.get(timer);
    std::lock_guard<std::mutex> g(e.m);
    e.timer_armed = true;
    e.timer_deadline = deadline_after(after.ns);
    e.cv.notify_all();
}

// --- ticks ---

Tick GetSystemTick() { return Tick{static_cast<int64_t>(static_cast<__int128>(now_ns()) * kTickFrequency / 1'000'000'000)}; }
int64_t GetSystemTickFrequency() { return kTickFrequency; }
TimeSpan ConvertToTimeSpan(Tick t) {
    return TimeSpan{static_cast<int64_t>(static_cast<__int128>(t.value) * 1'000'000'000 / kTickFrequency)};
}

// --- memory pages: commit/decommit inside a region the host reserved PROT_NONE ---

Result AllocateMemoryPages(uintptr_t addr, size_t size) {
    TRACE("AllocateMemoryPages %#lx %#zx", (unsigned long)addr, size);
    return mprotect(reinterpret_cast<void*>(addr), size, PROT_READ | PROT_WRITE) == 0 ? kSuccess : 1;
}
Result FreeMemoryPages(uintptr_t addr, size_t size) {
    TRACE("FreeMemoryPages %#lx %#zx", (unsigned long)addr, size);
    madvise(reinterpret_cast<void*>(addr), size, MADV_DONTNEED);
    return mprotect(reinterpret_cast<void*>(addr), size, PROT_NONE) == 0 ? kSuccess : 1;
}

HostSymbols symbols({
    {"_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mii", (void*)&CreateThread},
    {"_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE", (void*)&StartThread},
    {"_ZN2nn2os10WaitThreadEPNS0_10ThreadTypeE", (void*)&WaitThread},
    {"_ZN2nn2os13DestroyThreadEPNS0_10ThreadTypeE", (void*)&DestroyThread},
    {"_ZN2nn2os16GetCurrentThreadEv", (void*)&GetCurrentThread},
    {"_ZN2nn2os13SetThreadNameEPNS0_10ThreadTypeEPKc", (void*)&SetThreadName},
    {"_ZN2nn2os20GetThreadNamePointerEPKNS0_10ThreadTypeE", (void*)&GetThreadNamePointer},
    {"_ZN2nn2os17GetThreadPriorityEPKNS0_10ThreadTypeE", (void*)&GetThreadPriority},
    {"_ZN2nn2os20GetCurrentCoreNumberEv", (void*)&GetCurrentCoreNumber},
    {"_ZN2nn2os26GetThreadAvailableCoreMaskEv", (void*)&GetThreadAvailableCoreMask},
    {"_ZN2nn2os11SleepThreadENS_8TimeSpanE", (void*)&SleepThread},
    {"_ZN2nn2os11YieldThreadEv", (void*)&YieldThread},

    {"_ZN2nn2os15InitializeMutexEPNS0_9MutexTypeEbi", (void*)&InitializeMutex},
    {"_ZN2nn2os13FinalizeMutexEPNS0_9MutexTypeE", (void*)&FinalizeMutex},
    {"_ZN2nn2os9LockMutexEPNS0_9MutexTypeE", (void*)&LockMutex},
    {"_ZN2nn2os12TryLockMutexEPNS0_9MutexTypeE", (void*)&TryLockMutex},
    {"_ZN2nn2os11UnlockMutexEPNS0_9MutexTypeE", (void*)&UnlockMutex},
    {"_ZN2nn2os28IsMutexLockedByCurrentThreadEPKNS0_9MutexTypeE", (void*)&IsMutexLockedByCurrentThread},

    {"_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE", (void*)&InitializeEvent},
    {"_ZN2nn2os13FinalizeEventEPNS0_9EventTypeE", (void*)&FinalizeEvent},
    {"_ZN2nn2os11SignalEventEPNS0_9EventTypeE", (void*)&SignalEvent},
    {"_ZN2nn2os9WaitEventEPNS0_9EventTypeE", (void*)&WaitEvent},
    {"_ZN2nn2os14TimedWaitEventEPNS0_9EventTypeENS_8TimeSpanE", (void*)&TimedWaitEvent},
    {"_ZN2nn2os20InitializeTimerEventEPNS0_14TimerEventTypeENS0_14EventClearModeE", (void*)&InitializeTimerEvent},
    {"_ZN2nn2os18FinalizeTimerEventEPNS0_14TimerEventTypeE", (void*)&FinalizeTimerEvent},
    {"_ZN2nn2os16SignalTimerEventEPNS0_14TimerEventTypeE", (void*)&SignalTimerEvent},
    {"_ZN2nn2os14WaitTimerEventEPNS0_14TimerEventTypeE", (void*)&WaitTimerEvent},
    {"_ZN2nn2os22StartOneShotTimerEventEPNS0_14TimerEventTypeENS_8TimeSpanE", (void*)&StartOneShotTimerEvent},

    {"_ZN2nn2os13GetSystemTickEv", (void*)&GetSystemTick},
    {"_ZN2nn2os22GetSystemTickFrequencyEv", (void*)&GetSystemTickFrequency},
    {"_ZN2nn2os17ConvertToTimeSpanENS0_4TickE", (void*)&ConvertToTimeSpan},
    {"_ZN2nn2os19AllocateMemoryPagesEmm", (void*)&AllocateMemoryPages},
    {"_ZN2nn2os15FreeMemoryPagesEmm", (void*)&FreeMemoryPages},
});

}  // namespace
