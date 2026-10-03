#include "host/sample_window.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>

static void require(bool okay, const char* message) {
  if (!okay) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
  using xlle::HostSampleWindows;
  using xlle::HostSampleProgressRange;
  // Check the independent half-open interval rule at every boundary, including
  // the former 10.5B clamp and the declared route's 27B endpoint.
  for (size_t i = 0; i < HostSampleWindows::count; ++i) {
    const uint64_t first = i * 1500000000ull;
    require(HostSampleWindows::index(first) == i, "boundary enters next bucket");
    if (i) require(HostSampleWindows::index(first - 1) == i - 1, "last byte of prior interval");
    require(HostSampleWindows::begin(i) == first, "nominal interval start");
  }
  require(HostSampleWindows::index(27000000000ull) == 18, "27B has its own interval");
  require(!HostSampleWindows::is_tail(18), "27B is not clamped");
  const auto last = HostSampleWindows::count - 1;
  const uint64_t max = std::numeric_limits<uint64_t>::max();
  require(HostSampleWindows::is_tail(last), "last bucket is unbounded");
  require(HostSampleWindows::index(max) == last, "maximum progress never wraps");
  require(HostSampleWindows::index(1000000000000ull) == last, "long run remains bounded");
  HostSampleProgressRange range;
  require(!range.seen, "no manufactured initial sample");
  for (auto progress : {uint64_t(27000000000ull), uint64_t(10500000000ull), max, uint64_t(0)})
    range.record(progress);
  require(range.seen && range.minimum == 0 && range.maximum == max,
          "inclusive extrema retain backwards and maximum progress");
  std::puts("Host sample window boundaries passed");
}
