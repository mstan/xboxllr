#pragma once

#include <array>
#include <cstdint>

namespace xlle {

// Scalar NV2A DXT1 reconstruction before filtering. The October16 physical
// original/controlled/range batches establish texture-relative phase, palette
// role independence and shared channel limits. See docs/NV2A-DXT1-NOISE.md for
// the measured scope and the remaining one-unit filtering precision boundary.
// This is an executable fitted rule, not a claim about internal silicon stages.
inline std::array<uint8_t, 3> nv2a_dxt1_reconstruct(
    const std::array<uint8_t, 4>& palette, uint32_t x, uint32_t y, bool noise) {
    if (!palette[3]) return {{0, 0, 0}};
    std::array<uint8_t, 3> result{};
    const unsigned x0=x&1u, x1=(x>>1)&1u, y0=y&1u, y1=(y>>1)&1u;
    const unsigned k=((x0^y1)<<2)|(y0<<1)|(x1^y1);
    const unsigned phase[3]={(k+3u)&7u,
        (2u*(x0^y1)+4u-x1-y0)&3u, (10u-k)&7u};
    for (unsigned c=0;c<3;++c) {
        const unsigned n=c==1?2:3, step=1u<<n;
        const unsigned p=palette[c];
        if (!noise) {
            const unsigned code=p>>n;
            result[c]=static_cast<uint8_t>((code<<n)|(code>>(8-2*n)));
        } else {
            // The modulo is unsigned and bounded; ties select the lower byte.
            const int q=int(p)+int((phase[c]+256u-p+step/2)&(step-1))-int(step/2);
            result[c]=static_cast<uint8_t>(q<int(3*step/2-1)?0:
                                         q>=int(255-step/2)?255:q);
        }
    }
    return result;
}

} // namespace xlle
