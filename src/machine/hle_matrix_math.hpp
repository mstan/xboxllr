#pragma once
#include <array>

namespace xlle {
using HleMatrix = std::array<float, 16>;
// Native float arithmetic is intentional. The HLE contract permits nominal
// rounding differences from the guest x87 implementation.
inline HleMatrix hle_matrix_multiply(const HleMatrix &a, const HleMatrix &b) {
  HleMatrix out;
  for (unsigned r = 0; r < 4; ++r)
    for (unsigned c = 0; c < 4; ++c)
      out[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] +
                       a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
  return out;
}
} // namespace xlle
