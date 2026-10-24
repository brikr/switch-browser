#pragma once

// Loads a decompressed Horizon NRO (AArch64) into memory, links it against other modules and
// host symbols, and runs its initializer. This replaces what nn::ro / ldr:ro does on a Switch.

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct NroSymbol {
    std::string name;
    uint8_t type;     // STT_*
    uint8_t binding;  // STB_*
    uint16_t shndx;   // 0 = undefined (import)
    uint64_t value;
};

class NroModule {
public:
    // Maps the image; does not relocate.
    NroModule(std::string name, const std::string& path);

    const std::string& name() const { return name_; }
    uintptr_t base() const { return reinterpret_cast<uintptr_t>(image_); }
    size_t size() const { return size_; }
    bool contains(uintptr_t addr) const { return addr >= base() && addr < base() + size_; }
    const std::vector<NroSymbol>& symbols() const { return symbols_; }

    // Address of an exported symbol in this module, or 0.
    uintptr_t find_export(const std::string& name) const;

    // Name of the closest exported symbol at or below addr (for diagnostics).
    std::string describe(uintptr_t addr) const;

    template <typename Fn>
    void relocate(Fn&& resolve);  // resolve(const NroSymbol&) -> uintptr_t

    void run_init();

private:
    void apply_relr(uint64_t relr, uint64_t size);

    std::string name_;
    uint8_t* image_ = nullptr;
    size_t size_ = 0;
    std::unordered_map<int64_t, uint64_t> dynamic_;
    std::vector<NroSymbol> symbols_;
    std::unordered_map<std::string, uintptr_t> exports_;
};

// --- template implementation ---

namespace nro_detail {
constexpr uint32_t R_AARCH64_ABS64 = 257;
constexpr uint32_t R_AARCH64_GLOB_DAT = 1025;
constexpr uint32_t R_AARCH64_JUMP_SLOT = 1026;
constexpr uint32_t R_AARCH64_RELATIVE = 1027;
[[noreturn]] void bad_reloc(const std::string& module, uint32_t type, uint64_t offset);
}  // namespace nro_detail

template <typename Fn>
void NroModule::relocate(Fn&& resolve) {
    using namespace nro_detail;
    auto value = [&](int64_t tag) { auto it = dynamic_.find(tag); return it == dynamic_.end() ? 0 : it->second; };

    if (value(36 /*DT_RELR*/)) apply_relr(value(36), value(35 /*DT_RELRSZ*/));

    struct Rela { uint64_t offset; uint64_t info; int64_t addend; };
    auto apply = [&](uint64_t table, uint64_t bytes) {
        auto* r = reinterpret_cast<const Rela*>(image_ + table);
        for (size_t i = 0; i < bytes / sizeof(Rela); i++) {
            uint32_t type = static_cast<uint32_t>(r[i].info);
            uint32_t sym = static_cast<uint32_t>(r[i].info >> 32);
            auto* where = reinterpret_cast<uint64_t*>(image_ + r[i].offset);
            switch (type) {
                case R_AARCH64_RELATIVE:
                    *where = base() + r[i].addend;
                    break;
                case R_AARCH64_ABS64:
                case R_AARCH64_GLOB_DAT:
                case R_AARCH64_JUMP_SLOT: {
                    uintptr_t s = sym ? resolve(symbols_.at(sym)) : 0;
                    *where = s + r[i].addend;
                    break;
                }
                default:
                    bad_reloc(name_, type, r[i].offset);
            }
        }
    };
    if (value(7 /*DT_RELA*/)) apply(value(7), value(8 /*DT_RELASZ*/));
    if (value(23 /*DT_JMPREL*/)) apply(value(23), value(2 /*DT_PLTRELSZ*/));
}
