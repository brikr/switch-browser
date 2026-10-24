// POSIX threads, as imported directly by core_3.
//
// The engine allocates pthread_mutex_t / pthread_cond_t / pthread_attr_t with Nintendo musl's
// sizes, which are smaller than glibc's, so those objects map to host objects through address-keyed
// side tables instead of being handed to glibc. pthread_t values, pthread_once and pthread_join go
// to glibc directly (pthread_t is an opaque 8-byte handle in both, once_t an int initialised to 0).

#include <pthread.h>

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <mutex>

#include "../host_symbols.h"
#include "../log.h"
#include "sync_table.h"

namespace {

struct HostMutex {
    std::mutex m;
};
struct HostCond {
    std::condition_variable cv;
};
struct HostAttr {
    size_t stack_size = 0;
};

SyncTable<HostMutex> g_mutexes;
SyncTable<HostCond> g_conds;
SyncTable<HostAttr> g_attrs;

int mutex_init(void* m, const void*) {
    g_mutexes.reset(m, new HostMutex());
    return 0;
}
int mutex_destroy(void* m) {
    g_mutexes.erase(m);
    return 0;
}
int mutex_lock(void* m) {
    g_mutexes.get(m).m.lock();
    return 0;
}
int mutex_unlock(void* m) {
    g_mutexes.get(m).m.unlock();
    return 0;
}

int cond_init(void* c, const void*) {
    g_conds.reset(c, new HostCond());
    return 0;
}
int cond_destroy(void* c) {
    g_conds.erase(c);
    return 0;
}
int cond_wait(void* c, void* m) {
    std::unique_lock<std::mutex> l(g_mutexes.get(m).m, std::adopt_lock);
    g_conds.get(c).cv.wait(l);
    l.release();
    return 0;
}
int cond_signal(void* c) {
    g_conds.get(c).cv.notify_one();
    return 0;
}
int cond_broadcast(void* c) {
    g_conds.get(c).cv.notify_all();
    return 0;
}

int attr_init(void* a) {
    g_attrs.reset(a, new HostAttr());
    return 0;
}
int attr_destroy(void* a) {
    g_attrs.erase(a);
    return 0;
}
int attr_setstacksize(void* a, size_t size) {
    g_attrs.get(a).stack_size = size;
    return 0;
}

int thread_create(pthread_t* out, const void* attr, void* (*start)(void*), void* arg) {
    pthread_attr_t host;
    pthread_attr_init(&host);
    size_t stack = attr ? g_attrs.get(attr).stack_size : 0;
    // Room for host shim frames on top of what the engine asked for.
    pthread_attr_setstacksize(&host, std::max<size_t>(stack, 0x40000) + 0x40000);
    int rc = pthread_create(out, &host, start, arg);
    pthread_attr_destroy(&host);
    TRACE("pthread_create(stack %#zx) = %d", stack, rc);
    return rc;
}

HostSymbols symbols({
    {"pthread_mutex_init", (void*)&mutex_init},
    {"pthread_mutex_destroy", (void*)&mutex_destroy},
    {"pthread_mutex_lock", (void*)&mutex_lock},
    {"pthread_mutex_unlock", (void*)&mutex_unlock},
    {"pthread_cond_init", (void*)&cond_init},
    {"pthread_cond_destroy", (void*)&cond_destroy},
    {"pthread_cond_wait", (void*)&cond_wait},
    {"pthread_cond_signal", (void*)&cond_signal},
    {"pthread_cond_broadcast", (void*)&cond_broadcast},
    {"pthread_attr_init", (void*)&attr_init},
    {"pthread_attr_destroy", (void*)&attr_destroy},
    {"pthread_attr_setstacksize", (void*)&attr_setstacksize},
    {"pthread_create", (void*)&thread_create},
    {"pthread_join", (void*)&pthread_join},
    {"pthread_once", (void*)&pthread_once},
});

}  // namespace
