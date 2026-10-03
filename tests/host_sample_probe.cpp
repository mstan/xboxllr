// Exercise the real Windows sampler and report writer without guest assets.
#include "host/sample_profiler.hpp"
#include <limits>
#include <chrono>

int main() {
  std::atomic<uint64_t> progress{0};
  xlle::HostSampleProfiler profiler;
  profiler.set_progress_source(&progress);
  profiler.start(2);
  // Exact interval decisions also have deterministic unit coverage. Long dwell
  // periods here let the actual asynchronous sampler observe every test stage.
  for (uint64_t value : {uint64_t(0), uint64_t(10500000000ull), uint64_t(27000000000ull),
                         uint64_t(46500000000ull), std::numeric_limits<uint64_t>::max()}) {
    progress.store(value, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  profiler.stop_and_report();
}
