#pragma once

// Maps engine-owned synchronisation objects (whose internal layout is Nintendo's) to host objects,
// keyed by address. Objects are created lazily on first use, so statically-initialised engine
// objects work without an explicit init call. Sharded to keep lock contention low.

#include <array>
#include <mutex>
#include <unordered_map>

template <typename T>
class SyncTable {
public:
    T& get(const void* key) {
        Shard& s = shards_[(reinterpret_cast<uintptr_t>(key) >> 4) % shards_.size()];
        std::lock_guard<std::mutex> g(s.lock);
        auto& slot = s.map[key];
        if (!slot) slot = new T();
        return *slot;
    }

    // Replaces any existing object (used by explicit Initialize* calls).
    T& reset(const void* key, T* fresh) {
        Shard& s = shards_[(reinterpret_cast<uintptr_t>(key) >> 4) % shards_.size()];
        std::lock_guard<std::mutex> g(s.lock);
        auto& slot = s.map[key];
        delete slot;
        slot = fresh;
        return *slot;
    }

    void erase(const void* key) {
        Shard& s = shards_[(reinterpret_cast<uintptr_t>(key) >> 4) % shards_.size()];
        std::lock_guard<std::mutex> g(s.lock);
        auto it = s.map.find(key);
        if (it == s.map.end()) return;
        delete it->second;
        s.map.erase(it);
    }

private:
    struct Shard {
        std::mutex lock;
        std::unordered_map<const void*, T*> map;
    };
    std::array<Shard, 64> shards_;
};
