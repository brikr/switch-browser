// Templates for engine -> host call gates (copied and patched at runtime by cfi_gate.cpp).
//
// core_3 is built with Nintendo's "nncfi":
//  - every indirect call (including PLT calls) checks that the word before the target is
//    0xe7ffdefe, and traps otherwise;
//  - function prologues mix a hash keyed by x18 into the return address, and epilogues undo it,
//    so x18 must not change while an engine frame is live.
// Host code (glibc especially) treats x18 as a scratch register, so gates save x18 and the return
// address on a per-thread shadow stack (not the machine stack, so stack-passed and variadic
// arguments are untouched), call the host function, and restore both.

    .text

// Full gate: marker, x18/LR preservation.
    .balign 16
    .global nxb_gate_full_begin
    .global nxb_gate_full_entry
    .global nxb_gate_full_tls_offset
    .global nxb_gate_full_target
    .global nxb_gate_full_end
nxb_gate_full_begin:
    .word 0xe7ffdefe
nxb_gate_full_entry:
    mrs     x16, tpidr_el0
    ldr     x17, nxb_gate_full_tls_offset
    add     x16, x16, x17           // x16 = &shadow (struct { u64 idx; u64 slots[]; })
    ldr     x17, [x16]
    add     x17, x17, #2
    str     x17, [x16]              // idx += 2
    add     x16, x16, x17, lsl #3   // &slots[old idx] == x16 - 8
    stp     x18, x30, [x16, #-8]
    ldr     x16, nxb_gate_full_target
    blr     x16
    mrs     x16, tpidr_el0
    ldr     x17, nxb_gate_full_tls_offset
    add     x16, x16, x17
    ldr     x17, [x16]
    sub     x17, x17, #2
    str     x17, [x16]              // idx -= 2
    add     x16, x16, x17, lsl #3   // &slots[new idx] == x16 + 8
    ldp     x18, x30, [x16, #8]
    ret
    .balign 8
nxb_gate_full_tls_offset:
    .quad 0
nxb_gate_full_target:
    .quad 0
nxb_gate_full_end:

// Jump gate: marker only. For functions that return twice or never (setjmp/longjmp), which
// don't touch x18 themselves.
    .balign 16
    .global nxb_gate_jump_begin
    .global nxb_gate_jump_entry
    .global nxb_gate_jump_target
    .global nxb_gate_jump_end
nxb_gate_jump_begin:
    .word 0xe7ffdefe
nxb_gate_jump_entry:
    ldr     x16, nxb_gate_jump_target
    br      x16
    .balign 8
nxb_gate_jump_target:
    .quad 0
nxb_gate_jump_end:
