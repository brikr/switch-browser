#include "host/handlers.h"

#include <sys/mman.h>

#include "host/run_loop.h"
#include "log.h"

namespace nxb {

bool MemoryHandler::checkMemoryAvailability(size_t, bool) { return true; }
bool MemoryHandler::checkMemoryAllocatable(size_t, AllocationReason) { return true; }

#if NXB_CORE == 0
void* MemoryHandler::notifyMemoryExhaust(size_t request_size, size_t& out_allocated_size) {
    LOG("engine: memory exhausted (request %zu bytes)", request_size);
    out_allocated_size = 0;
    return nullptr;
}
#else
void MemoryHandler::notifyMemoryExhaust(size_t request_size, HeapType heap_type) {
    LOG("engine: memory exhausted (request %zu bytes, heap type %d)", request_size, (int)heap_type);
}
#endif

void MemoryHandler::notifyMemoryAllocationError(size_t request_size, AllocationReason reason) {
    LOG("engine: allocation of %zu bytes failed (reason %d)", request_size, (int)reason);
}

void MemoryHandler::notifyCrash(const char* file, int line, const char* function, const char* assertion) {
    FATAL("engine crash: %s at %s:%d (%s)", assertion ? assertion : "?", file ? file : "?", line,
          function ? function : "?");
}

#if NXB_CORE == 0
void MemoryHandler::notifyStackOverflow(bool need_restart, size_t stack_size, size_t consumption, size_t margin,
                                        void*, void*, void*, const char* file, int line, const char* function) {
    LOG("engine: stack overflow (size %zu, used %zu, margin %zu, restart %d) at %s:%d (%s)", stack_size, consumption,
        margin, need_restart, file ? file : "?", line, function ? function : "?");
}
#else
void MemoryHandler::notifyStackOverflow(size_t stack_size, size_t consumption, size_t margin, void*, void*, void*,
                                        const char* file, int line, const char* function) {
    LOG("engine: stack overflow (size %zu, used %zu, margin %zu) at %s:%d (%s)", stack_size, consumption, margin,
        file ? file : "?", line, function ? function : "?");
}
#endif

bool MemoryHandler::allocateMemoryPages(void* ptr, size_t size) {
    return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
}

void MemoryHandler::freeMemoryPages(void* ptr, size_t size) {
    madvise(ptr, size, MADV_DONTNEED);
    mprotect(ptr, size, PROT_NONE);
}

#if NXB_CORE == 0
bool TimerHandler::requestWakeUp(void* in_timer, bool (*)(void*), void* in_data) {
    RunLoop::main().post_wakeup(in_timer, in_data);
    return true;
}

void TimerHandler::cancelWakeUp(void* in_timer) {
    RunLoop::main().cancel_wakeups(in_timer);
}
#endif

}  // namespace nxb
