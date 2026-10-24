#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "nro_module.h"

class Linker {
public:
    // Loads modules in the given order (dependencies first), from dir/<name>.nro.
    void load(const std::string& dir, const std::vector<std::string>& names);
    // Resolves every import: other modules' exports, then host symbols, then trap stubs.
    void link();
    // Runs each module's _init in load order.
    void run_init();

    uintptr_t find_export(const std::string& name) const;
    std::string describe(uintptr_t addr) const;

    static Linker& instance();

private:
    struct Export {
        uintptr_t addr;
        bool weak;
    };

    void build_export_table();
    uintptr_t resolve(const NroModule& from, const NroSymbol& sym);
    uintptr_t make_trap(const std::string& name);

    std::vector<std::unique_ptr<NroModule>> modules_;
    std::unordered_map<std::string, Export> exports_;
    std::vector<std::string> unresolved_;
};
