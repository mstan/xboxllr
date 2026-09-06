#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "nv2a_vsh_capture.hpp"

namespace xlle::vsh_batch_capture {

constexpr uint32_t kMagic = 0x48425358u;  // XSBH, little-endian word stream.
constexpr uint32_t kVersion = 1u;
constexpr uint32_t kEndian = 0x01020304u;
constexpr uint32_t kBatchRecord = 0x42415443u;  // CTAB
constexpr uint32_t kEnd = 0x454e4421u;
constexpr uint32_t kMaxBatches = 64u;
constexpr uint32_t kMaxOps = vsh_capture::kMaxOps;
constexpr uint32_t kMaxExecutions = 16384u;
constexpr uint32_t kMaxReferences = 16384u;
constexpr uint32_t kMaxExpectedIndices = kMaxReferences * 3u;
constexpr uint32_t kCacheEntries = 1024u;

struct Execution {
    std::array<uint32_t, 64> inputs{};
    // Raw consumed outputs in vsh_capture::kConsumedOutputs order followed by
    // the actual postprocessed position used by raster setup.
    std::array<uint32_t, 32> expected{};
};

struct Batch {
    uint64_t sequence = 0;
    uint64_t fingerprint = 0;  // Reporting only; never an admission key.
    uint32_t primitive_mode = 0;
    bool large_target = false;
    bool cull_enabled = false;
    bool cull_front = false;
    bool cache_enabled = false;
    bool expected_decline = false;
    std::vector<vsh_capture::Op> ops;
    std::array<uint32_t, 768> constants{};
    std::vector<Execution> executions;
    // One source index and one execution-reference id per submitted vertex.
    std::vector<uint32_t> source_indices;
    std::vector<uint32_t> references;
    // Optional CPU-authoritative resident-primary expansion, expressed as
    // execution ids in final triangle order.
    std::vector<uint32_t> expected_indices;
};

namespace detail {

inline uint32_t cache_slot(uint32_t index) {
    const uint32_t mixed = index ^ (index >> 4) ^ (index >> 9);
    return mixed & (kCacheEntries - 1u);
}

inline uint32_t flags_from_batch(const Batch& batch) {
    return (batch.large_target ? 1u : 0u) |
           (batch.cull_enabled ? 2u : 0u) |
           (batch.cull_front ? 4u : 0u) |
           (batch.cache_enabled ? 8u : 0u) |
           (batch.expected_decline ? 16u : 0u);
}

inline void flags_to_batch(uint32_t flags, Batch& batch) {
    if (flags & ~31u) throw std::runtime_error("Invalid VSH batch flags");
    batch.large_target = (flags & 1u) != 0;
    batch.cull_enabled = (flags & 2u) != 0;
    batch.cull_front = (flags & 4u) != 0;
    batch.cache_enabled = (flags & 8u) != 0;
    batch.expected_decline = (flags & 16u) != 0;
}

inline void validate_mapping(const Batch& batch) {
    if (batch.ops.size() > kMaxOps || batch.executions.size() > kMaxExecutions ||
        batch.references.size() > kMaxReferences ||
        batch.source_indices.size() != batch.references.size() ||
        batch.expected_indices.size() > kMaxExpectedIndices) {
        throw std::runtime_error("VSH batch capture exceeds bounded limits");
    }
    for (uint32_t id : batch.references) {
        if (id >= batch.executions.size())
            throw std::runtime_error("VSH batch reference execution id is out of range");
    }
    for (uint32_t id : batch.expected_indices) {
        if (id >= batch.executions.size())
            throw std::runtime_error("VSH batch expected execution id is out of range");
    }

    if (!batch.cache_enabled) {
        if (batch.executions.size() != batch.references.size())
            throw std::runtime_error("Uncached VSH batch must execute every reference");
        for (std::size_t i = 0; i < batch.references.size(); ++i) {
            if (batch.references[i] != i)
                throw std::runtime_error("Uncached VSH batch mapping is not sequential");
        }
        return;
    }

    std::array<uint8_t, kCacheEntries> valid{};
    std::array<uint32_t, kCacheEntries> cached_source{};
    std::array<uint32_t, kCacheEntries> cached_execution{};
    uint32_t next_execution = 0;
    for (std::size_t i = 0; i < batch.references.size(); ++i) {
        const uint32_t source = batch.source_indices[i];
        const uint32_t slot = cache_slot(source);
        uint32_t expected = 0;
        if (valid[slot] && cached_source[slot] == source) {
            expected = cached_execution[slot];
        } else {
            if (next_execution >= batch.executions.size())
                throw std::runtime_error("VSH batch cache mapping has too few executions");
            expected = next_execution++;
            valid[slot] = 1;
            cached_source[slot] = source;
            cached_execution[slot] = expected;
        }
        if (batch.references[i] != expected)
            throw std::runtime_error("VSH batch cache mapping does not match direct cache");
    }
    if (next_execution != batch.executions.size())
        throw std::runtime_error("VSH batch executions are not chronological");
}

inline void validate_batch(const Batch& batch) {
    if (!batch.sequence)
        throw std::runtime_error("VSH batch sequence must be nonzero");
    if (batch.primitive_mode != 5u && batch.primitive_mode != 6u &&
        batch.primitive_mode != 7u)
        throw std::runtime_error("VSH batch primitive mode is outside resident-primary scope");
    validate_mapping(batch);
    if (batch.expected_decline && !batch.expected_indices.empty())
        throw std::runtime_error("Declined VSH batch has expanded indices");
    if (batch.expected_indices.size() % 3u)
        throw std::runtime_error("VSH batch expanded indices are not triangles");
    std::size_t max_expected = 0;
    if (batch.references.size() >= 3u) {
        if (batch.primitive_mode == 5u)
            max_expected = (batch.references.size() / 3u) * 3u;
        else
            max_expected = (batch.references.size() - 2u) * 3u;
    }
    if (batch.expected_indices.size() > max_expected)
        throw std::runtime_error("VSH batch expanded index count exceeds primitive mode");
}

}  // namespace detail

class Writer {
public:
    explicit Writer(const char* path) : file_(path, std::ios::binary) {
        const uint32_t header[]{kMagic, kVersion, kEndian, 0u};
        write(header, 4);
    }

    bool good() const { return bool(file_); }
    uint32_t count() const { return count_; }

    bool append(const Batch& batch) {
        if (finished_ || count_ >= kMaxBatches ||
            (count_ && batch.sequence <= last_sequence_)) {
            file_.setstate(std::ios::failbit);
            return false;
        }
        try {
            detail::validate_batch(batch);
        } catch (const std::runtime_error&) {
            file_.setstate(std::ios::failbit);
            return false;
        }

        const uint32_t header[]{
            kBatchRecord,
            static_cast<uint32_t>(batch.sequence),
            static_cast<uint32_t>(batch.sequence >> 32),
            static_cast<uint32_t>(batch.fingerprint),
            static_cast<uint32_t>(batch.fingerprint >> 32),
            batch.primitive_mode,
            detail::flags_from_batch(batch),
            static_cast<uint32_t>(batch.ops.size()),
            static_cast<uint32_t>(batch.executions.size()),
            static_cast<uint32_t>(batch.references.size()),
            static_cast<uint32_t>(batch.expected_indices.size()),
            0u,
        };
        write(header, 12);
        for (const auto& op : batch.ops) write(op.words.data(), op.words.size());
        write(batch.constants.data(), batch.constants.size());
        for (const auto& execution : batch.executions) {
            write(execution.inputs.data(), execution.inputs.size());
            write(execution.expected.data(), execution.expected.size());
        }
        write(batch.source_indices.data(), batch.source_indices.size());
        write(batch.references.data(), batch.references.size());
        write(batch.expected_indices.data(), batch.expected_indices.size());
        if (good()) {
            last_sequence_ = batch.sequence;
            ++count_;
        }
        return good();
    }

    bool finish() {
        if (!finished_) {
            const uint32_t footer[]{kEnd, count_};
            write(footer, 2);
            file_.flush();
            finished_ = true;
        }
        return good();
    }

private:
    void write(const uint32_t* words, std::size_t count) {
        if (count)
            file_.write(reinterpret_cast<const char*>(words),
                        static_cast<std::streamsize>(count * sizeof(uint32_t)));
    }

    std::ofstream file_;
    uint32_t count_ = 0;
    uint64_t last_sequence_ = 0;
    bool finished_ = false;
};

inline std::vector<Batch> read(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() > std::streamoff(512u * 1024u * 1024u))
        throw std::runtime_error("Missing or oversized VSH batch capture");
    file.seekg(0);
    const auto words = [&](uint32_t* dst, std::size_t count) {
        if (count && !file.read(reinterpret_cast<char*>(dst),
                                static_cast<std::streamsize>(count * sizeof(uint32_t))))
            throw std::runtime_error("Truncated VSH batch capture");
    };

    uint32_t file_header[4]{};
    words(file_header, 4);
    if (file_header[0] != kMagic || file_header[1] != kVersion ||
        file_header[2] != kEndian || file_header[3]) {
        throw std::runtime_error("Unsupported VSH batch capture format or byte order");
    }

    std::vector<Batch> result;
    for (;;) {
        uint32_t tag[2]{};
        words(tag, 2);
        if (tag[0] == kEnd) {
            if (tag[1] != result.size() || file.peek() != EOF)
                throw std::runtime_error("Invalid VSH batch capture footer");
            return result;
        }
        if (tag[0] != kBatchRecord || result.size() >= kMaxBatches)
            throw std::runtime_error("Invalid VSH batch capture record");

        uint32_t header[12]{};
        header[0] = tag[0];
        header[1] = tag[1];
        words(header + 2, 10);
        if (header[11]) throw std::runtime_error("Invalid VSH batch reserved field");
        if (header[7] > kMaxOps || header[8] > kMaxExecutions ||
            header[9] > kMaxReferences || header[10] > kMaxExpectedIndices) {
            throw std::runtime_error("VSH batch capture record exceeds bounded limits");
        }

        Batch batch;
        batch.sequence = uint64_t(header[1]) | (uint64_t(header[2]) << 32);
        batch.fingerprint = uint64_t(header[3]) | (uint64_t(header[4]) << 32);
        batch.primitive_mode = header[5];
        detail::flags_to_batch(header[6], batch);
        batch.ops.resize(header[7]);
        batch.executions.resize(header[8]);
        batch.source_indices.resize(header[9]);
        batch.references.resize(header[9]);
        batch.expected_indices.resize(header[10]);

        if (!batch.sequence || (!result.empty() && batch.sequence <= result.back().sequence))
            throw std::runtime_error("Non-increasing VSH batch sequence");
        for (auto& op : batch.ops) words(op.words.data(), op.words.size());
        words(batch.constants.data(), batch.constants.size());
        for (auto& execution : batch.executions) {
            words(execution.inputs.data(), execution.inputs.size());
            words(execution.expected.data(), execution.expected.size());
        }
        words(batch.source_indices.data(), batch.source_indices.size());
        words(batch.references.data(), batch.references.size());
        words(batch.expected_indices.data(), batch.expected_indices.size());
        detail::validate_batch(batch);
        result.push_back(std::move(batch));
    }
}

}  // namespace xlle::vsh_batch_capture
