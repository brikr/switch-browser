#include "cfi_gate.h"

#include <sys/mman.h>

#include <cstring>
#include <map>
#include <mutex>

#include "log.h"

extern "C" {
extern const uint8_t nxb_gate_full_begin[], nxb_gate_full_entry[], nxb_gate_full_tls_offset[],
    nxb_gate_full_target[], nxb_gate_full_end[];
extern const uint8_t nxb_gate_jump_begin[], nxb_gate_jump_entry[], nxb_gate_jump_target[], nxb_gate_jump_end[];
// Bounds of the host executable's code (GNU ld).
extern const char __executable_start[], etext[];
}

namespace nxb {
namespace {

bool in_host_text(uintptr_t a) {
    return a >= reinterpret_cast<uintptr_t>(__executable_start) && a < reinterpret_cast<uintptr_t>(etext);
}

// Per-thread shadow stack used by full gates: { index, slots[] } where each frame is (x18, lr).
struct ShadowStack {
    uint64_t index;
    uint64_t slots[8192];
};
thread_local ShadowStack t_shadow;

uintptr_t thread_pointer() {
    uintptr_t tp;
    asm volatile("mrs %0, tpidr_el0" : "=r"(tp));
    return tp;
}

// Static executable, local-exec TLS: the offset from the thread pointer is the same in every thread.
uint64_t shadow_tls_offset() {
    static uint64_t offset = reinterpret_cast<uintptr_t>(&t_shadow) - thread_pointer();
    return offset;
}

uint8_t* alloc_code(size_t size) {
    static std::mutex m;
    static uint8_t* page = nullptr;
    static size_t used = 0, cap = 0;
    std::lock_guard<std::mutex> g(m);
    size = (size + 15) & ~size_t(15);
    if (used + size > cap) {
        cap = 64 * 1024;
        void* p = mmap(nullptr, cap, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) FATAL("gate mmap failed");
        page = static_cast<uint8_t*>(p);
        used = 0;
    }
    uint8_t* out = page + used;
    used += size;
    return out;
}

}  // namespace

uintptr_t make_gate(const void* target, GateKind kind) {
    static std::mutex m;
    static std::map<std::pair<const void*, int>, uintptr_t> cache;
    std::lock_guard<std::mutex> g(m);
    auto key = std::make_pair(target, static_cast<int>(kind));
    if (auto it = cache.find(key); it != cache.end()) return it->second;

    const uint8_t *begin, *entry, *end, *target_slot, *tls_slot = nullptr;
    if (kind == GateKind::Full) {
        begin = nxb_gate_full_begin, entry = nxb_gate_full_entry, end = nxb_gate_full_end;
        target_slot = nxb_gate_full_target, tls_slot = nxb_gate_full_tls_offset;
    } else {
        begin = nxb_gate_jump_begin, entry = nxb_gate_jump_entry, end = nxb_gate_jump_end;
        target_slot = nxb_gate_jump_target;
    }
    uint8_t* code = alloc_code(end - begin);
    std::memcpy(code, begin, end - begin);
    uint64_t t = reinterpret_cast<uintptr_t>(target);
    std::memcpy(code + (target_slot - begin), &t, 8);
    if (tls_slot) {
        uint64_t off = shadow_tls_offset();
        std::memcpy(code + (tls_slot - begin), &off, 8);
    }
    __builtin___clear_cache(reinterpret_cast<char*>(code), reinterpret_cast<char*>(code + (end - begin)));
    uintptr_t gate = reinterpret_cast<uintptr_t>(code + (entry - begin));
    cache.emplace(key, gate);
    return gate;
}

void* gate_vtable_ptr(void* obj) {
    static std::mutex m;
    static std::map<const uintptr_t*, uintptr_t*> cache;
    auto** vptr_slot = static_cast<const uintptr_t**>(obj);
    const uintptr_t* vtable = *vptr_slot;
    std::lock_guard<std::mutex> g(m);
    auto it = cache.find(vtable);
    if (it == cache.end()) {
        // Itanium ABI: [-2] offset-to-top, [-1] typeinfo, then function pointers. The table ends
        // at the first entry that isn't host code (the next vtable's offset-to-top or typeinfo).
        size_t n = 0;
        while (in_host_text(vtable[n])) n++;
        if (n == 0) FATAL("gate_vtable: no virtual methods found at %p", (const void*)vtable);
        auto* copy = new uintptr_t[n + 2];
        copy[0] = vtable[-2];
        copy[1] = vtable[-1];
        for (size_t i = 0; i < n; i++) copy[2 + i] = make_gate(reinterpret_cast<const void*>(vtable[i]), GateKind::Full);
        it = cache.emplace(vtable, copy + 2).first;
        TRACE("gated vtable %p (%zu methods)", (const void*)vtable, n);
    }
    *vptr_slot = it->second;
    return obj;
}

}  // namespace nxb
