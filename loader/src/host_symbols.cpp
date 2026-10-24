#include "host_symbols.h"

#include <string>
#include <unordered_map>

#include "log.h"

namespace {

std::unordered_map<std::string, const void*>& registry() {
    static std::unordered_map<std::string, const void*> map;
    return map;
}

void add(const char* name, const void* addr) {
    if (!registry().emplace(name, addr).second) FATAL("host symbol registered twice: %s", name);
}

}  // namespace

HostSymbols::HostSymbols(std::initializer_list<std::pair<const char*, const void*>> symbols) {
    for (auto& [name, addr] : symbols) add(name, addr);
}

const void* find_host_symbol(const char* name) {
    static bool libc_added = [] {
        for (unsigned long i = 0; i < libc_symbol_count; i++) add(libc_symbols[i].name, libc_symbols[i].addr);
        return true;
    }();
    (void)libc_added;
    auto it = registry().find(name);
    return it == registry().end() ? nullptr : it->second;
}

bool trace_enabled() {
    static bool enabled = [] {
        const char* v = std::getenv("NXB_TRACE");
        return v && *v && *v != '0';
    }();
    return enabled;
}
