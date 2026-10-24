#pragma once

#include "host/wkc_api.h"

namespace nxb {

// Answers the engine's memory questions permissively and reports trouble.
class MemoryHandler : public WKC::WKCMemoryEventHandler {
public:
    bool checkMemoryAvailability(size_t request_size, bool in_forimage) override;
    bool checkMemoryAllocatable(size_t request_size, AllocationReason reason) override;
    void* notifyMemoryExhaust(size_t request_size, size_t& out_allocated_size) override;
    void notifyMemoryAllocationError(size_t request_size, AllocationReason reason) override;
    void notifyCrash(const char* file, int line, const char* function, const char* assertion) override;
    void notifyStackOverflow(bool need_restart, size_t stack_size, size_t consumption, size_t margin, void* stack_top,
                             void* stack_base, void* current_stack_top, const char* file, int line,
                             const char* function) override;
    bool allocateMemoryPages(void* ptr, size_t size) override;
    void freeMemoryPages(void* ptr, size_t size) override;
};

// Forwards engine timer wake-up requests to the main run loop.
class TimerHandler : public WKC::WKCTimerEventHandler {
public:
    bool requestWakeUp(void* in_timer, bool (*in_proc)(void*), void* in_data) override;
    void cancelWakeUp(void* in_timer) override;
};

}  // namespace nxb
