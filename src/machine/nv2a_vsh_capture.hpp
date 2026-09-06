#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace xlle::vsh_capture {

// Diagnostic wire format, little-endian uint32 words. Never serialize pointers
// or native structure padding. Captures contain guest data and must stay private.
constexpr uint32_t kMagic = 0x48535658u;
constexpr uint32_t kRecord = 0x52454344u;
constexpr uint32_t kEnd = 0x454e4421u;
constexpr uint32_t kMaxOps = 272;
constexpr uint32_t kMaxRecords = 16384;
constexpr std::array<unsigned, 7> kConsumedOutputs{0, 3, 4, 9, 10, 11, 12};

struct Op {
    // Six uvec4s: opcode/pair/arity/negate; bases/relative; offsets;
    // swizzles; destination bases+offsets; destination masks.
    std::array<uint32_t, 24> words{};
};
static_assert(sizeof(Op) == 96);

struct Vertex {
    uint64_t ordinal = 0;
    uint64_t fingerprint = 0; // reporting only; never admission or execution
    std::vector<Op> ops;
    std::array<uint32_t, 64> inputs{};
    std::array<uint32_t, 768> constants{};
    std::array<uint32_t, 28> expected{};
    std::array<uint32_t, 4> post_position{};
};

class Writer {
public:
    explicit Writer(const char* path) : file_(path, std::ios::binary) {
        const uint32_t header[]{kMagic, 1u, 0x01020304u, 0u};
        write(header, 4);
    }
    bool good() const { return bool(file_); }
    uint32_t count() const { return count_; }
    bool append(const Vertex& vertex) {
        if (finished_ || count_ >= kMaxRecords || vertex.ops.empty() ||
            vertex.ops.size() > kMaxOps) {
            file_.setstate(std::ios::failbit);
            return false;
        }
        const uint32_t header[]{kRecord, uint32_t(vertex.ops.size()),
            uint32_t(vertex.ordinal), uint32_t(vertex.ordinal >> 32),
            uint32_t(vertex.fingerprint), uint32_t(vertex.fingerprint >> 32)};
        write(header, 6);
        write(vertex.inputs.data(), vertex.inputs.size());
        write(vertex.constants.data(), vertex.constants.size());
        write(vertex.expected.data(), vertex.expected.size());
        write(vertex.post_position.data(), vertex.post_position.size());
        for (const auto& op : vertex.ops) write(op.words.data(), op.words.size());
        if (good()) ++count_;
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
    void write(const uint32_t* words, size_t count) {
        file_.write(reinterpret_cast<const char*>(words), count * sizeof(uint32_t));
    }
    std::ofstream file_;
    uint32_t count_ = 0;
    bool finished_ = false;
};

inline std::vector<Vertex> read(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() > std::streamoff(512u * 1024u * 1024u))
        throw std::runtime_error("Missing or oversized VSH capture");
    file.seekg(0);
    const auto words = [&](uint32_t* dst, size_t count) {
        if (!file.read(reinterpret_cast<char*>(dst), count * sizeof(uint32_t)))
            throw std::runtime_error("Truncated VSH capture");
    };
    uint32_t header[4]{};
    words(header, 4);
    if (header[0] != kMagic || header[1] != 1 || header[2] != 0x01020304u || header[3])
        throw std::runtime_error("Unsupported VSH capture format or byte order");
    std::vector<Vertex> result;
    for (;;) {
        uint32_t record[6]{};
        words(record, 2);
        if (record[0] == kEnd) {
            if (record[1] != result.size() || result.empty() || file.peek() != EOF)
                throw std::runtime_error("Invalid VSH capture footer");
            return result;
        }
        if (record[0] != kRecord || !record[1] || record[1] > kMaxOps ||
            result.size() >= kMaxRecords)
            throw std::runtime_error("Invalid VSH capture record");
        words(record + 2, 4);
        Vertex vertex;
        vertex.ordinal = uint64_t(record[2]) | (uint64_t(record[3]) << 32);
        vertex.fingerprint = uint64_t(record[4]) | (uint64_t(record[5]) << 32);
        if (!vertex.ordinal || (!result.empty() && vertex.ordinal <= result.back().ordinal))
            throw std::runtime_error("Non-increasing VSH capture ordinal");
        words(vertex.inputs.data(), vertex.inputs.size());
        words(vertex.constants.data(), vertex.constants.size());
        words(vertex.expected.data(), vertex.expected.size());
        words(vertex.post_position.data(), vertex.post_position.size());
        vertex.ops.resize(record[1]);
        for (auto& op : vertex.ops) words(op.words.data(), op.words.size());
        result.push_back(std::move(vertex));
    }
}

} // namespace xlle::vsh_capture
