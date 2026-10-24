#pragma once

// Engine <-> host ABI glue for Nintendo's "nncfi" (see cfi_gate_templates.S). Everything the
// engine calls in the host must go through a gate:
//  - imported functions: the linker gates them when resolving imports;
//  - callbacks passed as function pointers: wrap them with gate_function();
//  - objects whose virtual methods the engine calls: wrap them with gate_vtable().

#include <cstdint>

namespace nxb {

enum class GateKind {
    Full,  // marker + x18/return-address preservation (normal functions)
    Jump,  // marker only (setjmp/longjmp)
};

// Returns an engine-callable entry point for `target`. Cached per target and kind.
uintptr_t make_gate(const void* target, GateKind kind);

template <typename Fn>
Fn gate_function(Fn fn) {
    return reinterpret_cast<Fn>(make_gate(reinterpret_cast<const void*>(fn), GateKind::Full));
}

// Points `obj`'s vtable pointer at a copy of its vtable whose entries are gated. The copy is
// cached per original vtable. Single-inheritance host classes only. Returns obj.
void* gate_vtable_ptr(void* obj);
template <typename T>
T* gate_vtable(T* obj) {
    return static_cast<T*>(gate_vtable_ptr(obj));
}

}  // namespace nxb
