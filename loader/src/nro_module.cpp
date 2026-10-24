#include "nro_module.h"

#include <sys/mman.h>

#include <cstring>
#include <fstream>
#include <iterator>

#include "log.h"

namespace {

constexpr int64_t DT_NULL = 0, DT_STRTAB = 5, DT_SYMTAB = 6, DT_STRSZ = 10;

template <typename T>
T read(const std::vector<uint8_t>& buf, size_t off) {
    if (off + sizeof(T) > buf.size()) FATAL("read past end of image at %#zx", off);
    T v;
    std::memcpy(&v, buf.data() + off, sizeof(T));
    return v;
}

}  // namespace

[[noreturn]] void nro_detail::bad_reloc(const std::string& module, uint32_t type, uint64_t offset) {
    FATAL("%s: unsupported relocation type %u at %#llx", module.c_str(), type, (unsigned long long)offset);
}

NroModule::NroModule(std::string name, const std::string& path) : name_(std::move(name)) {
    std::ifstream in(path, std::ios::binary);
    if (!in) FATAL("cannot open %s", path.c_str());
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (file.size() < 0x80 || std::memcmp(file.data() + 0x10, "NRO0", 4) != 0)
        FATAL("%s is not a decompressed NRO (run scripts/unlz4nro.py first)", path.c_str());

    uint32_t mod0 = read<uint32_t>(file, 4);
    if (std::memcmp(file.data() + mod0, "MOD0", 4) != 0) FATAL("%s: missing MOD0", path.c_str());
    int32_t dyn_rel = read<int32_t>(file, mod0 + 4);
    int32_t bss_end_rel = read<int32_t>(file, mod0 + 12);

    // NRO file offsets equal memory offsets; .bss follows .data and ends at MOD0.bss_end.
    size_t mem_end = std::max<size_t>(file.size(), mod0 + bss_end_rel);
    size_ = (mem_end + 0xFFF) & ~size_t(0xFFF);
    // TODO: map .text RX / .rodata R / .data RW instead of one RWX region.
    void* p = mmap(nullptr, size_, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) FATAL("%s: mmap of %#zx bytes failed", name_.c_str(), size_);
    image_ = static_cast<uint8_t*>(p);
    std::memcpy(image_, file.data(), file.size());

    for (uint64_t off = mod0 + dyn_rel;; off += 16) {
        int64_t tag = read<int64_t>(file, off);
        if (tag == DT_NULL) break;
        dynamic_[tag] = read<uint64_t>(file, off + 8);
    }

    uint64_t symtab = dynamic_.at(DT_SYMTAB), strtab = dynamic_.at(DT_STRTAB), strsz = dynamic_.at(DT_STRSZ);
    if (strtab <= symtab) FATAL("%s: expected .dynstr to follow .dynsym", name_.c_str());
    for (uint64_t off = symtab; off < strtab; off += 24) {
        uint32_t name_off = read<uint32_t>(file, off);
        if (name_off >= strsz) break;
        NroSymbol s;
        s.name = reinterpret_cast<const char*>(image_ + strtab + name_off);
        uint8_t info = read<uint8_t>(file, off + 4);
        s.type = info & 0xF;
        s.binding = info >> 4;
        s.shndx = read<uint16_t>(file, off + 6);
        s.value = read<uint64_t>(file, off + 8);
        if (s.shndx != 0 && !s.name.empty() && (s.binding == 1 /*GLOBAL*/ || s.binding == 2 /*WEAK*/))
            exports_.emplace(s.name, base() + s.value);
        symbols_.push_back(std::move(s));
    }
    LOG("mapped %-10s at %#lx (%#zx bytes, %zu symbols, %zu exports)", name_.c_str(), (unsigned long)base(), size_,
        symbols_.size(), exports_.size());
}

uintptr_t NroModule::find_export(const std::string& name) const {
    auto it = exports_.find(name);
    return it == exports_.end() ? 0 : it->second;
}

std::string NroModule::describe(uintptr_t addr) const {
    const NroSymbol* best = nullptr;
    for (auto& s : symbols_)
        if (s.shndx != 0 && s.type == 2 /*FUNC*/ && base() + s.value <= addr && (!best || s.value > best->value))
            best = &s;
    char buf[64];
    std::snprintf(buf, sizeof buf, "+%#lx", (unsigned long)(addr - base()));
    std::string out = name_ + buf;
    if (best) {
        std::snprintf(buf, sizeof buf, "+%#lx", (unsigned long)(addr - base() - best->value));
        out += " (" + best->name + buf + ")";
    }
    return out;
}

void NroModule::apply_relr(uint64_t relr, uint64_t size) {
    auto* entries = reinterpret_cast<const uint64_t*>(image_ + relr);
    uint64_t* where = nullptr;
    for (size_t i = 0; i < size / 8; i++) {
        uint64_t e = entries[i];
        if ((e & 1) == 0) {
            where = reinterpret_cast<uint64_t*>(image_ + e);
            *where++ += base();
        } else {
            for (int bit = 0; (e >>= 1) != 0; bit++)
                if (e & 1) where[bit] += base();
            where += 63;
        }
    }
}

void NroModule::run_init() {
    uintptr_t init = find_export("_init");
    if (!init) FATAL("%s: no _init export", name_.c_str());
    __builtin___clear_cache(reinterpret_cast<char*>(image_), reinterpret_cast<char*>(image_ + size_));
    LOG("running %s _init", name_.c_str());
    int rc = reinterpret_cast<int (*)()>(init)();
    LOG("%s _init returned %d", name_.c_str(), rc);
}
