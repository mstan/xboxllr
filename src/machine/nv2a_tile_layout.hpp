/*
 * Adapted from envytools nvhw/tile.c, commit
 * f102b82381f3f11cee113d16374c87091db039d9.
 * Copyright (C) 2012 Marcelina Kościelnicka <mwk@0x04.net>
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#pragma once
#include <cstdint>
#include <limits>

namespace xlle {
// Uncompressed NV2A tile address layout, not texture Morton swizzling.
// NV20 address interleave with two partition bits and 4-KiB banks. The Xbox
// BlitBeyondWidth hardware golden independently qualifies pitch 2560/bank 1.
// Do not use envytools' generic IGP zero-partition shortcut for Xbox: it
// disagrees with that capture. Other memory-controller geometries, compression
// and CPU aperture selection are separate contracts. Controller integration
// must supply a tile-relative address and enforce its base/limit/ownership.
class Nv2aTileLayout {
public:
    bool configure(uint32_t pitch, uint32_t bank) {
        valid_ = false;
        if (bank > 1 || !pitch || (pitch & ~0xff00u)) return false;
        uint32_t factor = pitch >> 8, shift = 0;
        if (factor == 1) return false;
        while (!(factor & 1)) { factor >>= 1; ++shift; }
        if (factor != 1 && factor != 3 && factor != 5 && factor != 7 && factor != 13)
            return false;
        pitch_ = pitch; shift_ = shift; bank_ = bank; valid_ = true;
        return true;
    }

    bool translate(uint32_t linear, uint32_t& physical) const {
        if (!valid_) return false;
        const uint32_t byte_x = linear % pitch_, row = linear / pitch_;
        const uint32_t ix = byte_x & 255, iy = row & 15;
        const uint32_t x1 = ix & 15, x2 = ix >> 6;
        const uint32_t y1 = iy & 3, y2 = iy >> 2;
        const uint32_t partition = ((ix >> 4) + ((y2 & 1) << 1) + ((y2 & 2) >> 1)) & 3;
        uint64_t bank_address = uint64_t(row >> 4) * (pitch_ >> 8) + (byte_x >> 8);
        bank_address ^= bank_;
        if ((row & 16) && shift_) bank_address ^= 1;
        const uint32_t within = (((((y2 << 2) | x2) << 2) | y1) << 2 | partition) << 4 | x1;
        const uint64_t result = (bank_address << 12) | within;
        if (result > std::numeric_limits<uint32_t>::max()) return false;
        physical = static_cast<uint32_t>(result);
        return true;
    }

private:
    uint32_t pitch_ = 0, shift_ = 0, bank_ = 0;
    bool valid_ = false;
};
}
