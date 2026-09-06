#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace xlle {

class Nv2aVertexReuseShadow {
public:
    static constexpr std::size_t kSlots = 1024;
    static constexpr std::size_t kInputFloats = 16 * 4;
    static constexpr std::size_t kInputBytes = kInputFloats * sizeof(float);

    struct Stats {
        uint64_t batches = 0;
        uint64_t state_changes = 0;
        uint64_t seen = 0;
        uint64_t within_hits = 0;
        uint64_t cross_hits = 0;
        uint64_t misses = 0;
        uint64_t collisions = 0;
        uint64_t exact_compares = 0;
        uint64_t compared_bytes_requested = 0;
    };

    using ProgramWords = std::array<std::array<uint32_t, 4>, 136>;
    using Constants = std::array<std::array<float, 4>, 192>;

    static constexpr std::size_t entry_size_for_report();
    static constexpr std::size_t retained_bytes_for_report();

    static std::size_t selector_slot_for_test(const float* input64) {
        return selector_slot(input64);
    }

    void begin_batch(const ProgramWords& program, uint32_t start,
                     const Constants& constants, bool compiled) {
        ++stats_.batches;
        if (++current_batch_ == 0) {
            invalidate_all_entries();
            current_batch_ = 1;
        }

        if (!state_valid_ || state_start_ != start || state_compiled_ != compiled ||
            std::memcmp(state_program_.data(), program.data(),
                        sizeof(state_program_)) != 0 ||
            std::memcmp(state_constants_.data(), constants.data(),
                        sizeof(state_constants_)) != 0) {
            state_valid_ = true;
            state_start_ = start;
            state_compiled_ = compiled;
            std::memcpy(state_program_.data(), program.data(), sizeof(state_program_));
            std::memcpy(state_constants_.data(), constants.data(), sizeof(state_constants_));
            ++stats_.state_changes;
            advance_generation();
        }
    }

    void observe(const float* input64) {
        ++stats_.seen;
        if (!state_valid_ || generation_ == 0) {
            ++stats_.misses;
            return;
        }

        Entry& entry = entries_[selector_slot(input64)];
        if (entry.generation == generation_) {
            ++stats_.exact_compares;
            stats_.compared_bytes_requested += kInputBytes;
            if (std::memcmp(entry.input.data(), input64, kInputBytes) == 0) {
                if (entry.last_batch == current_batch_)
                    ++stats_.within_hits;
                else
                    ++stats_.cross_hits;
                entry.last_batch = current_batch_;
                return;
            }
            ++stats_.collisions;
        }

        ++stats_.misses;
        entry.generation = generation_;
        entry.last_batch = current_batch_;
        std::memcpy(entry.input.data(), input64, kInputBytes);
    }

    const Stats& stats() const { return stats_; }

private:
    struct Entry {
        uint64_t generation = 0;
        uint64_t last_batch = 0;
        std::array<uint8_t, kInputBytes> input{};
    };

    static uint32_t load_word(const float* input64, std::size_t word) {
        uint32_t bits = 0;
        std::memcpy(&bits, input64 + word, sizeof(bits));
        return bits;
    }

    static uint32_t mix(uint32_t value) {
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        value *= 0x846CA68Bu;
        value ^= value >> 16;
        return value;
    }

    static std::size_t selector_slot(const float* input64) {
        uint32_t selector = 0x9E3779B9u;
        selector ^= mix(load_word(input64, 0));
        selector ^= mix(load_word(input64, 1) + 0x85EBCA6Bu);
        selector ^= mix(load_word(input64, 2) + 0xC2B2AE35u);
        selector ^= mix(load_word(input64, 3 * 4) + 0x27D4EB2Fu);
        selector ^= mix(load_word(input64, 9 * 4) + 0x165667B1u);
        return static_cast<std::size_t>(selector & (kSlots - 1));
    }

    void advance_generation() {
        if (++generation_ == 0) {
            invalidate_all_entries();
            generation_ = 1;
        }
    }

    void invalidate_all_entries() {
        for (Entry& entry : entries_) entry.generation = 0;
    }

    Stats stats_{};
    uint64_t generation_ = 0;
    uint64_t current_batch_ = 0;
    bool state_valid_ = false;
    uint32_t state_start_ = 0;
    bool state_compiled_ = false;
    ProgramWords state_program_{};
    Constants state_constants_{};
    std::array<Entry, kSlots> entries_{};
};

constexpr std::size_t Nv2aVertexReuseShadow::entry_size_for_report() {
    return sizeof(Entry);
}

constexpr std::size_t Nv2aVertexReuseShadow::retained_bytes_for_report() {
    return kSlots * sizeof(Entry);
}

}  // namespace xlle
