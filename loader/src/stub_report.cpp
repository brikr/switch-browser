#include "stub_report.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "log.h"

namespace nxb {
namespace {

struct Entry {
    unsigned long calls = 0;
    bool expected = false;
};

std::mutex g_mutex;
std::unordered_map<std::string, Entry> g_stubs;
std::function<void(const char*)> g_listener;

bool show_expected() {
    static const bool all = [] {
        const char* v = std::getenv("NXB_STUBS");
        return v && !std::strcmp(v, "all");
    }();
    return all;
}

}  // namespace

void report_stub(const char* name, bool expected) {
    std::function<void(const char*)> listener;
    {
        std::lock_guard<std::mutex> l(g_mutex);
        Entry& e = g_stubs[name];
        e.expected = expected;
        if (e.calls++ != 0) {
            TRACE("stub %s", name);
            return;
        }
        if (expected && !show_expected()) return;
        listener = g_listener;
    }
    LOG("[stub] %s%s", name, expected ? " (expected)" : "");
    if (listener) listener(name);
}

void set_stub_listener(std::function<void(const char*)> listener) {
    std::lock_guard<std::mutex> l(g_mutex);
    g_listener = std::move(listener);
}

std::vector<StubCount> stub_counts() {
    std::vector<StubCount> out;
    {
        std::lock_guard<std::mutex> l(g_mutex);
        for (auto& [name, e] : g_stubs) out.push_back({name, e.calls, e.expected});
    }
    std::sort(out.begin(), out.end(), [](const StubCount& a, const StubCount& b) {
        if (a.expected != b.expected) return !a.expected;
        return a.calls != b.calls ? a.calls > b.calls : a.name < b.name;
    });
    return out;
}

void log_stub_summary() {
    auto counts = stub_counts();
    size_t expected = std::count_if(counts.begin(), counts.end(), [](const StubCount& c) { return c.expected; });
    size_t unexpected = counts.size() - expected;
    LOG("unimplemented stubs called: %zu (plus %zu expected no-ops%s)", unexpected, expected,
        show_expected() || !expected ? "" : "; NXB_STUBS=all lists them");
    for (auto& c : counts)
        if (!c.expected || show_expected()) LOG("  %8lu  %s%s", c.calls, c.name.c_str(), c.expected ? " (expected)" : "");
}

}  // namespace nxb
