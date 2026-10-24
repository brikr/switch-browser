#pragma once

// Registry of symbols the host provides to the engine modules (libc, libc++, nn:: SDK stand-ins).
// Shim files register their symbols with a static HostSymbols object, keyed by the exact
// (mangled) name the modules import.

#include <initializer_list>
#include <utility>

struct HostSymbols {
    HostSymbols(std::initializer_list<std::pair<const char*, const void*>> symbols);
};

const void* find_host_symbol(const char* name);

// C symbol table defined in shims/libc_table.c.
extern "C" {
#include "host_symbols_c.h"
}
