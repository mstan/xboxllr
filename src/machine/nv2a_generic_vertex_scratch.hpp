#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace xlle {

struct GenericVertexScratchStats {
    uint64_t packet_leases = 0;
    uint64_t expansion_leases = 0;
    uint64_t reuse_hits = 0;
    uint64_t oversized_releases = 0;
    uint64_t retained_bytes = 0;
};

template <typename T>
class GenericVectorScratchLease {
public:
    // Preconditions: construct only for an enabled scratch experiment and only
    // with a fresh local target vector whose contents are not live elsewhere.
    // The lease moves the member scratch into that target for the dynamic scope,
    // then returns capacity on destruction if the owner did not disable/re-enable
    // the experiment while callbacks were running.
    static constexpr size_t kMaxRetainedElements = 65536;

    GenericVectorScratchLease(std::vector<T>& scratch,
                              std::vector<T>& target,
                              bool& enabled,
                              uint64_t& epoch,
                              GenericVertexScratchStats& stats,
                              bool expansion)
        : scratch_(&scratch), target_(&target), enabled_(&enabled),
          epoch_(&epoch), captured_epoch_(epoch), stats_(&stats) {
        if (expansion) {
            ++stats_->expansion_leases;
        } else {
            ++stats_->packet_leases;
        }
        if (scratch.capacity() != 0) ++stats_->reuse_hits;
        target_->swap(scratch);
        target_->clear();
    }

    GenericVectorScratchLease(const GenericVectorScratchLease&) = delete;
    GenericVectorScratchLease& operator=(const GenericVectorScratchLease&) = delete;

    ~GenericVectorScratchLease() { release(); }

    void release() {
        if (!target_) return;
        target_->clear();
        const bool oversized = target_->capacity() > kMaxRetainedElements;
        if (oversized) {
            ++stats_->oversized_releases;
            std::vector<T>().swap(*target_);
        }
        if (*enabled_ && captured_epoch_ == *epoch_ && !oversized) {
            scratch_->clear();
            scratch_->swap(*target_);
        }
        scratch_ = nullptr;
        target_ = nullptr;
    }

private:
    std::vector<T>* scratch_ = nullptr;
    std::vector<T>* target_ = nullptr;
    bool* enabled_ = nullptr;
    uint64_t* epoch_ = nullptr;
    uint64_t captured_epoch_ = 0;
    GenericVertexScratchStats* stats_ = nullptr;
};

template <typename T>
inline size_t generic_vector_scratch_retained_bytes(
    const std::vector<T>& scratch) {
    return scratch.capacity() * sizeof(T);
}

template <typename T>
inline void generic_vector_scratch_release(std::vector<T>& scratch) {
    scratch.clear();
    std::vector<T>().swap(scratch);
}

}  // namespace xlle
