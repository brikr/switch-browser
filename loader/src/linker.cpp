#include "linker.h"

#include <sys/mman.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "host_symbols.h"
#include "log.h"

namespace {

// Zero-filled stand-in for unresolved data imports, so reads don't fault at load time.
alignas(4096) uint8_t g_unresolved_data[4096];

// Called (via a trap stub) when the engine calls an import nobody provides.
[[noreturn]] void unresolved_import_called(const char* name) {
    uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    std::fprintf(stderr, "[loader] FATAL: engine called unimplemented import %s\n", name);
    std::fprintf(stderr, "[loader]   called from %s\n", Linker::instance().describe(caller - 4).c_str());
    std::abort();
}

}  // namespace

Linker& Linker::instance() {
    static Linker linker;
    return linker;
}

void Linker::load(const std::string& dir, const std::vector<std::string>& names) {
    for (auto& n : names) modules_.push_back(std::make_unique<NroModule>(n, dir + "/" + n + ".nro"));
}

void Linker::build_export_table() {
    // Strong definitions win over weak ones; otherwise first module in load order.
    for (auto& m : modules_)
        for (auto& s : m->symbols()) {
            if (s.shndx == 0 || s.name.empty() || (s.binding != 1 && s.binding != 2)) continue;
            bool weak = s.binding == 2;
            auto [it, inserted] = exports_.try_emplace(s.name, Export{m->base() + s.value, weak});
            if (!inserted && it->second.weak && !weak) it->second = Export{m->base() + s.value, false};
        }
}

uintptr_t Linker::find_export(const std::string& name) const {
    auto it = exports_.find(name);
    return it == exports_.end() ? 0 : it->second.addr;
}

uintptr_t Linker::resolve(const NroModule& from, const NroSymbol& sym) {
    (void)from;
    if (uintptr_t a = find_export(sym.name)) return a;
    if (const void* h = find_host_symbol(sym.name.c_str())) return reinterpret_cast<uintptr_t>(h);
    if (std::find(unresolved_.begin(), unresolved_.end(), sym.name) == unresolved_.end())
        unresolved_.push_back(sym.name);
    if (sym.type == 1 /*STT_OBJECT*/) return reinterpret_cast<uintptr_t>(g_unresolved_data);
    return make_trap(sym.name);
}

// Emits: ldr x0, name; ldr x16, handler; br x16 — so the handler knows which import was hit.
uintptr_t Linker::make_trap(const std::string& name) {
    static std::unordered_map<std::string, uintptr_t> traps;
    static uint8_t* page = nullptr;
    static size_t used = 4096;
    if (auto it = traps.find(name); it != traps.end()) return it->second;
    if (used + 32 > 4096) {
        void* p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) FATAL("trap page mmap failed");
        page = static_cast<uint8_t*>(p);
        used = 0;
    }
    uint32_t* code = reinterpret_cast<uint32_t*>(page + used);
    code[0] = 0x58000080;  // ldr x0, #16
    code[1] = 0x580000B0;  // ldr x16, #20
    code[2] = 0xD61F0200;  // br x16
    code[3] = 0xD503201F;  // nop
    const char* stored = strdup(name.c_str());
    void* handler = reinterpret_cast<void*>(&unresolved_import_called);
    std::memcpy(page + used + 16, &stored, 8);
    std::memcpy(page + used + 24, &handler, 8);
    __builtin___clear_cache(reinterpret_cast<char*>(code), reinterpret_cast<char*>(code) + 32);
    uintptr_t addr = reinterpret_cast<uintptr_t>(code);
    used += 32;
    traps.emplace(name, addr);
    return addr;
}

void Linker::link() {
    build_export_table();
    for (auto& m : modules_) m->relocate([&](const NroSymbol& s) { return resolve(*m, s); });
    if (unresolved_.empty()) {
        LOG("linked %zu modules, all imports resolved", modules_.size());
        return;
    }
    LOG("linked %zu modules, %zu imports unresolved (trap if called):", modules_.size(), unresolved_.size());
    for (auto& n : unresolved_) LOG("  unresolved: %s", n.c_str());
}

void Linker::run_init() {
    for (auto& m : modules_) m->run_init();
}

std::string Linker::describe(uintptr_t addr) const {
    for (auto& m : modules_)
        if (m->contains(addr)) return m->describe(addr);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%#lx (host)", (unsigned long)addr);
    return buf;
}
