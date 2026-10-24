/* C view of the host symbol table (see host_symbols.h). */
#pragma once

struct host_sym {
    const char* name;
    const void* addr;
};
extern const struct host_sym libc_symbols[];
extern const unsigned long libc_symbol_count;
