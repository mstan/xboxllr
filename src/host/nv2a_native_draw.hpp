// Native primitive accounting adapted from NoRain211/doaxvb-re
// aa1d3c8afcd8d063171d819e27307070b63f056b, recomp-runtime/d3d_draw_model.c.
// GPL-3.0; see LICENSES/GPL-3.0.txt. Xbox primitive values are NV097
// encodings, not desktop D3DPRIMITIVETYPE (which are one lower).
#pragma once
#include <cstddef>
#include <cstdint>
namespace xlle {
inline size_t native_draw_triangle_count(uint32_t primitive, size_t vertices) {
    switch (primitive) {
    case 5: return vertices / 3;
    case 6: case 7: return vertices < 3 ? 0 : vertices - 2;
    default: return 0;
    }
}
}
