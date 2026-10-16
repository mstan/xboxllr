#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace xlle {

// Zero is the legacy hash namespace. Saturate at zero after exhausting the
// nonzero IDs; never wrap into an ID that a renderer may still retain.
inline uint64_t nv2a_allocate_texture_content_id(std::atomic<uint64_t>& next) {
    uint64_t id = next.load(std::memory_order_relaxed);
    while (id && !next.compare_exchange_weak(id, id + 1,
                                            std::memory_order_relaxed)) {}
    return id;
}

// Memoize a pure conversion AFTER the caller obtains a coherent source snapshot.
// Exact bytes, not a hash or guest address, decide reuse. Output remains owned by
// each draw packet. This does not cache guest memory or bypass a resolve/read.
class Nv2aTextureConversionCache {
public:
    // The measured working set needs 30,539,776 bytes. A 32 MiB trace
    // simulation reaches the reuse plateau; larger budgets add no hits.
    static constexpr size_t kBudget = 32 * 1024 * 1024;
    struct Entry {
        uint32_t width = 0, height = 0;
        bool linear = false, alpha8 = false;
        std::vector<uint8_t> source, output;
        uint64_t hash = 0, content_id = 0, age = 0;
        uint32_t source_pitch = 0, source_format = 0, conversion_profile = 0;
    };

    static uint64_t new_content_id() {
        // Shared by all device instances, independent of cache/renderer reset.
        static std::atomic<uint64_t> next{1};
        return nv2a_allocate_texture_content_id(next);
    }

    const Entry* find(const std::vector<uint8_t>& source, uint32_t width,
                      uint32_t height, bool linear, bool alpha8,
                      uint32_t source_pitch = 0, uint32_t source_format = 0,
                      uint32_t conversion_profile = 0) {
        for (auto& entry : entries_) {
            if (entry.age && entry.width == width && entry.height == height &&
                entry.linear == linear && entry.alpha8 == alpha8 &&
                entry.source_pitch == source_pitch &&
                entry.source_format == source_format &&
                entry.conversion_profile == conversion_profile &&
                entry.source == source) {
                entry.age = ++clock_;
                ++hits_;
                return &entry;
            }
        }
        ++misses_;
        return nullptr;
    }

    void store(const std::vector<uint8_t>& source, uint32_t width,
               uint32_t height, bool linear, bool alpha8,
               const std::vector<uint8_t>& output, uint64_t hash,
               uint64_t content_id = 0, uint32_t source_pitch = 0,
               uint32_t source_format = 0, uint32_t conversion_profile = 0) {
        if (source.size() > kBudget || output.size() > kBudget - source.size()) {
            ++oversize_;
            return;
        }
        const size_t bytes = source.size() + output.size();
        for (;;) {
            Entry* empty = nullptr;
            Entry* oldest = nullptr;
            for (auto& entry : entries_) {
                if (!entry.age) empty = &entry;
                else if (!oldest || entry.age < oldest->age) oldest = &entry;
            }
            if (empty && bytes_ <= kBudget - bytes) {
                *empty = Entry{width, height, linear, alpha8, source, output,
                               hash, content_id, ++clock_, source_pitch,
                               source_format, conversion_profile};
                bytes_ += bytes;
                return;
            }
            // A full slot table or byte budget evicts the least recent entry.
            bytes_ -= oldest->source.size() + oldest->output.size();
            *oldest = Entry{};
            ++evictions_;
        }
    }

    uint64_t hits() const { return hits_; }
    uint64_t misses() const { return misses_; }
    uint64_t evictions() const { return evictions_; }
    uint64_t oversize() const { return oversize_; }
    size_t bytes() const { return bytes_; }

private:
    std::array<Entry, 8> entries_{};
    uint64_t clock_ = 0, hits_ = 0, misses_ = 0, evictions_ = 0, oversize_ = 0;
    size_t bytes_ = 0;
};

} // namespace xlle
