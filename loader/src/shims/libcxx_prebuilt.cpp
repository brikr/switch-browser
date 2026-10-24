// libc++ (std::__1) internals the engine imports, taken from a prebuilt aarch64 libc++.a
// (Ubuntu's libc++-14-dev, unpacked into deps/ by scripts/fetch_deps.sh and linked statically),
// plus the libstdc++ pieces that share the Itanium ABI. Only declarations live here: the asm labels
// bind each name to the library's definition.

#include "../host_symbols.h"

#define DECLARE(var, mangled) extern "C" char var[] asm(mangled)

// libc++ 14
DECLARE(string_append, "_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6appendEPKcm");
DECLARE(string_resize, "_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6resizeEmc");
DECLARE(sw_get_deleter, "_ZNKSt3__119__shared_weak_count13__get_deleterERKSt9type_info");
DECLARE(sw_release_weak, "_ZNSt3__119__shared_weak_count14__release_weakEv");
DECLARE(sw_lock, "_ZNSt3__119__shared_weak_count4lockEv");
DECLARE(sw_dtor, "_ZNSt3__119__shared_weak_countD2Ev");
// Linux libc++ uses a 32-bit (futex) contention counter; Nintendo's uses `long`. The value passed
// to wait() always comes from monitor(), so the low 32 bits round-trip unchanged.
DECLARE(atomic_wait_i, "_ZNSt3__120__libcpp_atomic_waitEPVKvi");
DECLARE(atomic_notify_all, "_ZNSt3__123__cxx_atomic_notify_allEPVKv");
DECLARE(atomic_monitor, "_ZNSt3__123__libcpp_atomic_monitorEPVKv");
DECLARE(sort_a, "_ZNSt3__16__sortIRNS_6__lessIaaEEPaEEvT0_S5_T_");
DECLARE(sort_h, "_ZNSt3__16__sortIRNS_6__lessIhhEEPhEEvT0_S5_T_");
DECLARE(sort_s, "_ZNSt3__16__sortIRNS_6__lessIssEEPsEEvT0_S5_T_");
DECLARE(sort_t, "_ZNSt3__16__sortIRNS_6__lessIttEEPtEEvT0_S5_T_");
DECLARE(sort_l, "_ZNSt3__16__sortIRNS_6__lessIllEEPlEEvT0_S5_T_");
DECLARE(sort_m, "_ZNSt3__16__sortIRNS_6__lessImmEEPmEEvT0_S5_T_");
DECLARE(sort_d, "_ZNSt3__16__sortIRNS_6__lessIddEEPdEEvT0_S5_T_");

// libstdc++ (same Itanium ABI names)
DECLARE(set_new_handler_fn, "_ZSt15set_new_handlerPFvvE");
DECLARE(new_array, "_Znam");
DECLARE(new_array_nothrow, "_ZnamRKSt9nothrow_t");
DECLARE(dynamic_cast_fn, "__dynamic_cast");
DECLARE(class_type_info_vtable, "_ZTVN10__cxxabiv117__class_type_infoE");
DECLARE(si_class_type_info_vtable, "_ZTVN10__cxxabiv120__si_class_type_infoE");
DECLARE(vmi_class_type_info_vtable, "_ZTVN10__cxxabiv121__vmi_class_type_infoE");

namespace {

HostSymbols symbols({
    {"_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6appendEPKcm", string_append},
    {"_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6resizeEmc", string_resize},
    {"_ZNKSt3__119__shared_weak_count13__get_deleterERKSt9type_info", sw_get_deleter},
    {"_ZNSt3__119__shared_weak_count14__release_weakEv", sw_release_weak},
    {"_ZNSt3__119__shared_weak_count4lockEv", sw_lock},
    {"_ZNSt3__119__shared_weak_countD2Ev", sw_dtor},
    {"_ZNSt3__120__libcpp_atomic_waitEPVKvl", atomic_wait_i},
    {"_ZNSt3__123__cxx_atomic_notify_allEPVKv", atomic_notify_all},
    {"_ZNSt3__123__libcpp_atomic_monitorEPVKv", atomic_monitor},
    {"_ZNSt3__16__sortIRNS_6__lessIaaEEPaEEvT0_S5_T_", sort_a},
    {"_ZNSt3__16__sortIRNS_6__lessIhhEEPhEEvT0_S5_T_", sort_h},
    {"_ZNSt3__16__sortIRNS_6__lessIssEEPsEEvT0_S5_T_", sort_s},
    {"_ZNSt3__16__sortIRNS_6__lessIttEEPtEEvT0_S5_T_", sort_t},
    {"_ZNSt3__16__sortIRNS_6__lessIllEEPlEEvT0_S5_T_", sort_l},
    {"_ZNSt3__16__sortIRNS_6__lessImmEEPmEEvT0_S5_T_", sort_m},
    {"_ZNSt3__16__sortIRNS_6__lessIddEEPdEEvT0_S5_T_", sort_d},
    {"_ZSt15set_new_handlerPFvvE", set_new_handler_fn},
    {"_Znam", new_array},
    {"_ZnamRKSt9nothrow_t", new_array_nothrow},
    {"__dynamic_cast", dynamic_cast_fn},
    {"_ZTVN10__cxxabiv117__class_type_infoE", class_type_info_vtable},
    {"_ZTVN10__cxxabiv120__si_class_type_infoE", si_class_type_info_vtable},
    {"_ZTVN10__cxxabiv121__vmi_class_type_infoE", vmi_class_type_info_vtable},
});

}  // namespace
