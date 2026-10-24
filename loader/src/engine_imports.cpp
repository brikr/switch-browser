#include "engine_imports.h"

#include <cstdint>

#include "linker.h"
#include "log.h"

extern "C" {
extern uintptr_t nxb_engine_import_slots[];
extern const char* const nxb_engine_import_names[];
extern const uint64_t nxb_engine_import_count;
}

void bind_engine_imports(const Linker& linker) {
    size_t missing = 0;
    for (uint64_t i = 0; i < nxb_engine_import_count; i++) {
        uintptr_t addr = linker.find_export(nxb_engine_import_names[i]);
        if (!addr) {
            LOG("engine does not export %s (needed by host)", nxb_engine_import_names[i]);
            missing++;
        }
        nxb_engine_import_slots[i] = addr;
    }
    if (missing) FATAL("%zu host -> engine imports unresolved", missing);
    LOG("bound %llu host -> engine imports", (unsigned long long)nxb_engine_import_count);
}
