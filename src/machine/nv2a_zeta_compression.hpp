/*
 * NV20 Z24S8 decoder adapted from envytools nvhw/comp.c, commit
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
#include <array>
#include <cstdint>

namespace xlle {
// The original Xbox driver programs NV20-style ZCOMP (bit 26 selects Z24S8),
// not envytools' generic chipset-0x2a/NV25 format selector. Non-MS little-endian
// 4x4 Z24S8 blocks only. Compression tags and physical layout belong to Nv2a.
// The decoder follows the donor. The encoder is authored from that format;
// its choice of 8/16/64-byte representation is not a silicon compressor oracle.
struct Nv2aZetaCompression {
    using Pixels = std::array<uint32_t,16>;
    using Encoded = std::array<uint8_t,16>;

    static uint64_t load64(const uint8_t* p) {
        uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(i*8);return v;
    }
    static void store64(uint8_t* p,uint64_t v) {
        for(unsigned i=0;i<8;++i)p[i]=uint8_t(v>>(i*8));
    }
    static int32_t sign_extend(uint32_t v,unsigned bits) {
        const uint32_t mask=(1u<<bits)-1,sign=1u<<(bits-1);
        v&=mask;return (v&sign)?int32_t(v)-int32_t(mask+1):int32_t(v);
    }
    static bool anchor(unsigned x,unsigned y) {
        return (x==1 && (y==1 || y==2)) || (x==2 && y==1);
    }
    static void decode(const Encoded& encoded,Pixels& pixels) {
        const uint64_t w0=load64(encoded.data());
        const uint64_t w1=(w0>>63)?0:load64(encoded.data()+8);
        const uint32_t stencil=uint32_t(w0)&255,base=uint32_t(w0>>8)&0xffffff;
        const int32_t dx=sign_extend(uint32_t(w0>>32),15);
        const int32_t dy=sign_extend(uint32_t(w0>>47),15);
        unsigned bit=62;
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x) {
            uint32_t residual=0;
            if(!anchor(x,y))for(unsigned b=0;b<5;++b) {
                if(bit==63)++bit; // payload bit 63 is the 8-byte size flag
                residual|=uint32_t((bit<63?w0>>bit:w1>>(bit-64))&1)<<b;++bit;
            }
            // With MS disabled the donor's rounded half-unit expression is
            // exactly dx*(x-1)+dy*(y-1), including negative slopes.
            const int32_t delta=dx*(int32_t(x)-1)+dy*(int32_t(y)-1)+sign_extend(residual,5);
            pixels[y*4+x]=((base+uint32_t(delta))<<8)|stencil;
        }
    }

    // Zero means the caller must store all 64 bytes with a clear tag. Never
    // discard stencil changes, out-of-range slopes or unrepresentable residuals.
    static unsigned encode(const Pixels& pixels,Encoded& encoded) {
        const uint32_t stencil=pixels[5]&255,base=pixels[5]>>8;
        const int32_t dx=sign_extend((pixels[6]>>8)-base,24);
        const int32_t dy=sign_extend((pixels[9]>>8)-base,24);
        if(dx< -16384 || dx>16383 || dy< -16384 || dy>16383)return 0;
        uint64_t w0=uint64_t(pixels[5])|(uint64_t(uint32_t(dx)&0x7fff)<<32)|
                    (uint64_t(uint32_t(dy)&0x7fff)<<47),w1=0;
        unsigned bit=62;
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x) {
            const uint32_t p=pixels[y*4+x];if((p&255)!=stencil)return 0;
            const int32_t plane=int32_t(base)+dx*(int32_t(x)-1)+dy*(int32_t(y)-1);
            const int32_t delta=sign_extend((p>>8)-uint32_t(plane),24);
            if(anchor(x,y)){if(delta)return 0;continue;}
            if(delta< -16 || delta>15)return 0;
            for(unsigned b=0;b<5;++b) {
                if(bit==63)++bit;
                const uint64_t v=(uint32_t(delta)>>b)&1;
                if(bit<63)w0|=v<<bit;else w1|=v<<(bit-64);
                ++bit;
            }
        }
        const unsigned size=w1?16:8;if(size==8)w0|=uint64_t(1)<<63;
        store64(encoded.data(),w0);store64(encoded.data()+8,w1);return size;
    }
};
}
