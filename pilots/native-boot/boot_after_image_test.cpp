// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot_after_image.h"
#include <iostream>

int main() {
    unsigned checks = 0;
    auto require = [&](bool condition) { ++checks; if (!condition) throw std::runtime_error("after-image test failed"); };
    auto rejected = [&](std::vector<uint8_t> data) {
        try { BootAfterImage::decode(data.data(), data.size()); }
        catch (const std::runtime_error &) { ++checks; return; }
        throw std::runtime_error("invalid after-image accepted");
    };
    // Authored runs: long opaque-white1023, then677. No original bitmap data.
    std::vector<uint8_t> bytes{0xfe,0xff,0x96,0xfa};
    auto image = BootAfterImage::decode(bytes.data(),bytes.size());
    require(image.size()==6800);
    require(image.front()==204 && image[3]==255 && image[6796]==204);
    // Mixed short and long runs, including consumed zero-length controls.
    bytes={1,0x1f,0xda,0x0f,0x7a,0x0a}; // 7 gray1 +1014 black +670 black =1691, deliberately incomplete.
    rejected(bytes);
    rejected({0}); rejected({2}); rejected({0xfe,0xff});
    rejected({0xfe,0xff,0xfe,0xff}); rejected({});
    for (unsigned level=0;level<16;++level) {
        unsigned a=2|(1023<<2)|(level<<12), b=2|(677<<2)|(level<<12);
        bytes={uint8_t(a),uint8_t(a>>8),uint8_t(b),uint8_t(b>>8)};
        image=BootAfterImage::decode(bytes.data(),bytes.size());
        require(image[0]==uint8_t(level*204/15.0+.5));
    }
    bool missing=false;
    try { BootAfterImage::load({}); } catch (const std::runtime_error &) { missing=true; }
    require(missing);
    std::cout << checks << " after-image checks passed\n";
}
