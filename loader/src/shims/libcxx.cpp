// LLVM libc++ (std::__1) runtime functions and the C++ ABI bits the engine imports, plus
// Nintendo musl ("nnmusl") hooks. The host toolchain uses libstdc++, so these are reimplemented.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>

#include "../host_symbols.h"
#include "../log.h"
#include "sync_table.h"

extern "C" int __cxa_guard_acquire(int64_t*);
extern "C" void __cxa_guard_release(int64_t*);
extern "C" void __cxa_pure_virtual();

namespace {

// --- nnmusl ---

// Called from each module's _init with its dynamic-section layout. Returning 1 makes _init skip
// constructors; the loader has already relocated the module, so report "not initialised yet".
int nnmusl_init_dso(...) { return 0; }
int nnmusl_fini_dso(...) { return 0; }
int nnmusl_translate_fd(int fd) { return fd; }

// --- std::__1::mutex / condition_variable ---

struct HostMutex {
    std::mutex m;
};
struct HostCondVar {
    std::condition_variable cv;
};
SyncTable<HostMutex> g_mutexes;
SyncTable<HostCondVar> g_condvars;

void mutex_lock(void* self) { g_mutexes.get(self).m.lock(); }
bool mutex_try_lock(void* self) { return g_mutexes.get(self).m.try_lock(); }
void mutex_unlock(void* self) { g_mutexes.get(self).m.unlock(); }

// libc++ unique_lock<mutex> layout: { mutex* m; bool owns; }
struct UniqueLockLayout {
    void* mutex;
    bool owns;
};

void cv_wait(void* self, UniqueLockLayout* lock) {
    std::unique_lock<std::mutex> ul(g_mutexes.get(lock->mutex).m, std::adopt_lock);
    g_condvars.get(self).cv.wait(ul);
    ul.release();
}
void cv_notify_one(void* self) { g_condvars.get(self).cv.notify_one(); }
void cv_notify_all(void* self) { g_condvars.get(self).cv.notify_all(); }

// --- chrono (libc++: steady_clock in ns, system_clock in µs) ---

int64_t steady_now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
int64_t system_now() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// --- misc libc++ internals ---

// Smallest prime >= n (libc++ semantics, used by unordered containers).
size_t next_prime(size_t n) {
    if (n <= 2) return 2;
    if (n % 2 == 0) n++;
    for (;; n += 2) {
        bool prime = true;
        for (size_t d = 3; d * d <= n; d += 2)
            if (n % d == 0) {
                prime = false;
                break;
            }
        if (prime) return n;
    }
}

void sort_int(int* first, int* last, void*) { std::sort(first, last); }
void sort_uint(unsigned* first, unsigned* last, void*) { std::sort(first, last); }

[[noreturn]] void verbose_abort(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "[engine] libc++ abort: ");
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::abort();
}

// --- operator new/delete (engine's own allocations go through its peers; these are leftovers) ---

void* op_new(size_t n) {
    void* p = std::malloc(n ? n : 1);
    if (!p) FATAL("operator new(%zu) failed", n);
    return p;
}
void* op_new_nothrow(size_t n, const void*) { return std::malloc(n ? n : 1); }
void op_delete(void* p) { std::free(p); }
void op_delete_sized(void* p, size_t) { std::free(p); }

HostSymbols symbols({
    {"__nnmusl_init_dso", (void*)&nnmusl_init_dso},
    {"__nnmusl_fini_dso", (void*)&nnmusl_fini_dso},
    {"_nnmusl_socket_TranslateNnmuslFileDescriptorOrPassThrough", (void*)&nnmusl_translate_fd},

    {"__cxa_guard_acquire", (void*)&__cxa_guard_acquire},
    {"__cxa_guard_release", (void*)&__cxa_guard_release},
    {"__cxa_pure_virtual", (void*)&__cxa_pure_virtual},

    {"_ZNSt3__15mutex4lockEv", (void*)&mutex_lock},
    {"_ZNSt3__15mutex8try_lockEv", (void*)&mutex_try_lock},
    {"_ZNSt3__15mutex6unlockEv", (void*)&mutex_unlock},
    {"_ZNSt3__118condition_variable4waitERNS_11unique_lockINS_5mutexEEE", (void*)&cv_wait},
    {"_ZNSt3__118condition_variable10notify_oneEv", (void*)&cv_notify_one},
    {"_ZNSt3__118condition_variable10notify_allEv", (void*)&cv_notify_all},
    {"_ZNSt3__16chrono12steady_clock3nowEv", (void*)&steady_now},
    {"_ZNSt3__16chrono12system_clock3nowEv", (void*)&system_now},
    {"_ZNSt3__112__next_primeEm", (void*)&next_prime},
    {"_ZNSt3__16__sortIRNS_6__lessIiiEEPiEEvT0_S5_T_", (void*)&sort_int},
    {"_ZNSt3__16__sortIRNS_6__lessIjjEEPjEEvT0_S5_T_", (void*)&sort_uint},
    {"_ZNSt3__122__libcpp_verbose_abortEPKcz", (void*)&verbose_abort},

    {"_Znwm", (void*)&op_new},
    {"_ZnwmRKSt9nothrow_t", (void*)&op_new_nothrow},
    {"_ZdlPv", (void*)&op_delete},
    {"_ZdlPvm", (void*)&op_delete_sized},
    {"_ZdaPv", (void*)&op_delete},
    {"_ZSt7nothrow", (const void*)&std::nothrow},
});

}  // namespace
