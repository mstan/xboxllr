// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// Authored bounded equivalent of the owned kernel's after-image RLE/gray
// conversion. All artwork and quad data come from the user's private inputs.
struct BootAfterImage {
    unsigned left = 0, top = 0;
    std::vector<uint8_t> rgba;
    static std::vector<uint8_t> decode(const uint8_t *data, size_t bytes) {
        if (!data || !bytes || bytes > 4096)
            throw std::runtime_error("Invalid startup after-image size");
        std::vector<uint8_t> out;
        out.reserve(100 * 17 * 4);
        for (size_t i = 0; i < bytes;) {
            unsigned run, level;
            if (data[i] & 1) {
                run = (data[i] >> 1) & 7;
                level = data[i++] >> 4;
            } else if (data[i] & 2) {
                if (i + 2 > bytes)
                    throw std::runtime_error("Truncated startup after-image run");
                unsigned word = data[i] | (unsigned(data[i + 1]) << 8);
                i += 2;
                run = (word >> 2) & 1023;
                level = word >> 12;
            } else
                throw std::runtime_error("Invalid startup after-image run flags");
            if (run > 1700 - out.size() / 4)
                throw std::runtime_error("Startup after-image exceeds its original texture");
            // Original helper: truncate(level * 204 / 15.0 + 0.5).
            uint8_t gray = uint8_t((level * 204 + 7) / 15);
            for (unsigned n = 0; n < run; ++n)
                out.insert(out.end(), {gray, gray, gray, 255});
        }
        if (out.size() != 100 * 17 * 4)
            throw std::runtime_error("Incomplete startup after-image texture");
        return out;
    }
    struct Range { const uint8_t *data; size_t bytes; };
    static uint32_t word(const uint8_t *p) {
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    static float scalar(const uint8_t *p) {
        uint32_t bits=word(p);
        float value;
        static_assert(sizeof(value)==sizeof(bits), "Startup quad requires32-bit float");
        std::memcpy(&value,&bits,sizeof(value));
        return value;
    }
    static Range range(const std::vector<uint8_t> &pack, const char *name) {
        if (pack.size() < 16 || std::memcmp(pack.data(), "XBPK", 4) || word(pack.data()+4) != 1)
            throw std::runtime_error("Invalid startup pack header");
        size_t count = word(pack.data()+8);
        if (count > 64 || 16 + count * 32 > pack.size())
            throw std::runtime_error("Invalid startup pack inventory");
        for (size_t i = 0; i < count; ++i) {
            const auto *entry = pack.data() + 16 + i * 32;
            if (std::strncmp(reinterpret_cast<const char *>(entry + 16), name, 16))
                continue;
            size_t bytes = word(entry+4), offset = word(entry+8);
            if (offset < 16 + count * 32 || offset > pack.size() || bytes > pack.size()-offset)
                throw std::runtime_error("Invalid startup after-image extent");
            return {pack.data()+offset, bytes};
        }
        throw std::runtime_error("Startup pack lacks required after-image data; prepare it again");
    }
    static BootAfterImage load(const std::vector<uint8_t> &pack,
                              const std::vector<uint8_t> &title_logo = {}) {
        auto image = range(pack, "after_image"), quad = range(pack, "after_quad"), gray = range(pack, "after_gray");
        if (quad.bytes != 144 || gray.bytes != 8)
            throw std::runtime_error("Invalid startup after-image geometry");
        float vertices[36], constants[2];
        for (unsigned i=0;i<36;++i) vertices[i]=scalar(quad.data+i*4);
        for (unsigned i=0;i<2;++i) constants[i]=scalar(gray.data+i*4);
        if (constants[0] != 15 || constants[1] != .5f)
            throw std::runtime_error("Unsupported startup after-image gray conversion");
        float left = 640, top = 480, right = 0, bottom = 0;
        for (unsigned i = 0; i < 6; ++i) {
            const float *v = vertices + i * 6;
            for (unsigned j = 0; j < 6; ++j)
                if (!std::isfinite(v[j])) throw std::runtime_error("Invalid startup quad value");
            if (v[2] != .5f || v[3] != 1 || (v[4] != 0 && v[4] != 100) || (v[5] != 0 && v[5] != 17))
                throw std::runtime_error("Unsupported startup after-image quad");
            left = std::min(left,v[0]); right = std::max(right,v[0]);
            top = std::min(top,v[1]); bottom = std::max(bottom,v[1]);
        }
        if (left < 0 || top < 0 || right > 640 || bottom > 480 || right-left != 100 || bottom-top != 17 ||
            std::floor(left) != left || std::floor(top) != top)
            throw std::runtime_error("Unsupported startup after-image placement");
        return {unsigned(left), unsigned(top), title_logo.empty() ? decode(image.data,image.bytes) :
                                                               decode(title_logo.data(),title_logo.size())};
    }
};
