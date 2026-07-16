// xlle — always-on ring buffer (PRINCIPLES §7)
//
// Records continuously from process start, in Release too. Overwrites the oldest
// entry when full. Probes QUERY a window after the fact; they never arm-then-run.
// Fixed power-of-two capacity, single-writer / multi-reader friendly for the debug
// server (the writer is the emulation thread; readers snapshot).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace xlle {

template <typename T>
class Ring {
public:
    explicit Ring(size_t capacity_pow2) : mask_(capacity_pow2 - 1), buf_(capacity_pow2) {
        // capacity must be a power of two so (seq & mask_) wraps correctly.
        // (checked at call sites; kept branch-free here on the hot path)
    }

    // Hot path: append one entry. Never blocks, never allocates.
    void push(const T& v) {
        const uint64_t seq = head_.load(std::memory_order_relaxed);
        buf_[seq & mask_] = v;
        head_.store(seq + 1, std::memory_order_release);
    }

    // Total entries ever pushed (monotonic; the ring only retains the last capacity()).
    uint64_t total() const { return head_.load(std::memory_order_acquire); }
    size_t capacity() const { return mask_ + 1; }

    // Copy the most recent `n` entries (oldest-first) into out. Clamped to what is
    // still retained. This is the QUERY primitive — a bounded window, never a firehose.
    size_t window(size_t n, std::vector<T>& out) const {
        const uint64_t h = head_.load(std::memory_order_acquire);
        const size_t cap = capacity();
        const size_t avail = (h < cap) ? static_cast<size_t>(h) : cap;
        if (n > avail) n = avail;
        out.clear();
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const uint64_t seq = h - n + i;
            out.push_back(buf_[seq & mask_]);
        }
        return n;
    }

private:
    const uint64_t mask_;
    std::vector<T> buf_;
    std::atomic<uint64_t> head_{0};
};

}  // namespace xlle
