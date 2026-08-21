// Opt-in host-side sampled profiler for xlle (Windows).
//
// Doctrine fit: this is an always-available observability probe, not a behavior
// change. It is OFF unless `--host-sample-profile <period_ms>` is passed; it
// never touches guest state, device state, or timing, so replay hashes are
// unaffected. It samples ONLY the main emulation thread at a fixed host period.
//
// Per guest-instruction window (1.5B instructions) it reports:
//   * leaf RIP attribution — module-relative offsets into fixed preallocated
//     tables (no heap allocation on the sampler thread). RIPs outside every
//     loaded module report as `[no-module]` (Unicorn's JIT code cache).
//   * blocking-call attribution — when the leaf RIP is outside xlle.exe (a
//     kernel syscall stub, CRT copy routine, or GL driver), one StackWalk64
//     pass finds the first xlle.exe caller frame; top leaf+caller pairs are
//     reported so stalls attribute to the call site that issued them.
//
// Report lines go to stdout as `[host-sampler] ...` and are symbolized offline
// by tools/symbolize_host_profile.py against the RelWithDebInfo exe.
//
// Known limits (stated, not hidden): single-threaded attribution only — GL
// driver worker threads are not sampled directly (they surface as main-thread
// waits); stack walks can fail across JIT frames, in which case the pair is
// reported with caller=0.
#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <dbghelp.h>

namespace xlle {

class HostSampleProfiler {
 public:
  static constexpr size_t kTableSize = 1u << 15;      // power of two
  static constexpr size_t kPairTableSize = 1u << 13;  // power of two
  static constexpr size_t kMaxReportEntries = 24;
  static constexpr size_t kMaxPairEntries = 16;
  static constexpr size_t kWindowCount = 8;
  static constexpr uint64_t kWindowInsns = 1500000000ull;

  void set_progress_source(const std::atomic<uint64_t>* progress) {
    progress_ = progress;
  }

  void start(uint32_t period_ms) {
    if (worker_.joinable()) return;
    period_ms_ = period_ms > 0 ? period_ms : 2;
    process_handle_ = GetCurrentProcess();
    process_id_ = GetCurrentProcessId();
    module_base_ = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    MODULEINFO main_info{};
    if (GetModuleInformation(process_handle_, GetModuleHandleW(nullptr),
                             &main_info, sizeof(main_info))) {
      module_size_ = main_info.SizeOfImage;
    }
    main_thread_id_ = GetCurrentThreadId();
    snapshot_modules();
    sym_initialized_ =
        SymInitialize(process_handle_, nullptr, FALSE) == TRUE;
    windows_ = std::make_unique<std::array<WindowTable, kWindowCount>>();
    stop_.store(false, std::memory_order_relaxed);
    worker_ = std::thread([this] { sample_loop(); });
  }

  // Stops the sampler and prints the aggregated per-window report to stdout.
  void stop_and_report() {
    if (!worker_.joinable()) return;
    stop_.store(true, std::memory_order_relaxed);
    worker_.join();
    report();
    if (sym_initialized_) {
      SymCleanup(process_handle_);
      sym_initialized_ = false;
    }
  }

 private:
  struct Bucket {
    uint32_t rva;
    uint32_t module;
    uint64_t count;
  };
  // Leaf-outside-module sample paired with the first xlle.exe caller frame:
  // attributes blocking syscalls/copies to the xlle call site that issued them.
  struct PairBucket {
    uint32_t caller_rva;
    uint32_t leaf_module;
    uint32_t leaf_rva;
    uint64_t count;
  };
  struct WindowTable {
    std::array<Bucket, kTableSize> buckets{};
    std::array<PairBucket, kPairTableSize> pairs{};
    size_t used = 0;
    size_t pairs_used = 0;
    uint64_t samples = 0;
    uint64_t leaf_outside_samples = 0;
  };
  struct ModuleRange {
    uintptr_t base;
    size_t size;
    uint32_t id;  // index into module_names_
  };

  // Snapshots the loaded module list once so per-sample attribution is a
  // binary search instead of an API call.
  void snapshot_modules() {
    module_names_.clear();
    module_ranges_.clear();
    module_names_.push_back("[no-module]");
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process_id_);
    if (snapshot != INVALID_HANDLE_VALUE) {
      MODULEENTRY32W entry{};
      entry.dwSize = sizeof(entry);
      if (Module32FirstW(snapshot, &entry)) {
        do {
          ModuleRange range{};
          range.base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
          range.size = entry.modBaseSize;
          range.id = static_cast<uint32_t>(module_names_.size());
          char name[MAX_PATH];
          WideCharToMultiByte(CP_UTF8, 0, entry.szModule, -1, name,
                              sizeof(name), nullptr, nullptr);
          module_names_.push_back(name);
          module_ranges_.push_back(range);
        } while (Module32NextW(snapshot, &entry));
      }
      CloseHandle(snapshot);
    }
    std::sort(module_ranges_.begin(), module_ranges_.end(),
              [](const ModuleRange& a, const ModuleRange& b) {
                return a.base < b.base;
              });
  }

  // Returns the matched range or nullptr; rva receives rip relative to base
  // (or the raw rip when no module matches).
  const ModuleRange* locate(uintptr_t rip, uint32_t& rva) const {
    size_t low = 0, high = module_ranges_.size();
    while (low < high) {
      const size_t mid = (low + high) / 2;
      if (rip < module_ranges_[mid].base) {
        high = mid;
      } else if (rip >= module_ranges_[mid].base + module_ranges_[mid].size) {
        low = mid + 1;
      } else {
        rva = static_cast<uint32_t>(rip - module_ranges_[mid].base);
        return &module_ranges_[mid];
      }
    }
    rva = static_cast<uint32_t>(rip);
    return nullptr;
  }

  void record_leaf(WindowTable& table, uint32_t module_id, uint32_t rva) {
    const uint64_t key = (static_cast<uint64_t>(module_id) << 32) | rva;
    size_t slot = static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> 48) &
                  (kTableSize - 1);
    for (size_t probes = 0; probes < 64; ++probes) {
      Bucket& bucket = table.buckets[slot];
      if (bucket.count == 0) {
        bucket.rva = rva;
        bucket.module = module_id;
        bucket.count = 1;
        ++table.used;
        return;
      }
      if (bucket.rva == rva && bucket.module == module_id) {
        ++bucket.count;
        return;
      }
      slot = (slot + 1) & (kTableSize - 1);
    }
    ++overflow_;  // probe budget exhausted: drop rather than allocate
  }

  void record_pair(WindowTable& table, uint32_t leaf_module, uint32_t leaf_rva,
                   uint32_t caller_rva) {
    ++table.leaf_outside_samples;
    const uint64_t key = ((static_cast<uint64_t>(leaf_module) << 48) ^
                          (static_cast<uint64_t>(leaf_rva) << 16) ^
                          caller_rva) | 1ull;
    size_t slot = static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> 48) &
                  (kPairTableSize - 1);
    for (size_t probes = 0; probes < 32; ++probes) {
      PairBucket& bucket = table.pairs[slot];
      if (bucket.count == 0) {
        bucket.leaf_rva = leaf_rva;
        bucket.leaf_module = leaf_module;
        bucket.caller_rva = caller_rva;
        bucket.count = 1;
        ++table.pairs_used;
        return;
      }
      if (bucket.leaf_rva == leaf_rva && bucket.leaf_module == leaf_module &&
          bucket.caller_rva == caller_rva) {
        ++bucket.count;
        return;
      }
      slot = (slot + 1) & (kPairTableSize - 1);
    }
    ++overflow_;
  }

  // RVA of the first stack frame inside the xlle module, or ~0u when unknown.
  uint32_t first_xlle_caller(HANDLE thread, const CONTEXT& context) const {
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rsp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    CONTEXT walk_context = context;
    for (unsigned depth = 0; depth < 28; ++depth) {
      if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process_handle_, thread,
                       &frame, &walk_context, nullptr,
                       SymFunctionTableAccess64, SymGetModuleBase64,
                       nullptr)) {
        break;
      }
      const uintptr_t pc = static_cast<uintptr_t>(frame.AddrPC.Offset);
      if (!pc) break;
      if (pc >= module_base_ && pc < module_base_ + module_size_) {
        return static_cast<uint32_t>(pc - module_base_);
      }
    }
    // Meta-unwind failed (GL ICD frames often break it): fall back to a raw
    // stack scan for the nearest value that is an xlle code address.
    uint8_t stack[16384];
    SIZE_T read = 0;
    const uintptr_t rsp = static_cast<uintptr_t>(context.Rsp);
    if (ReadProcessMemory(process_handle_,
                          reinterpret_cast<LPCVOID>(rsp), stack,
                          sizeof(stack), &read) &&
        read >= sizeof(uintptr_t)) {
      for (SIZE_T offset = 0; offset + sizeof(uintptr_t) <= read;
           offset += sizeof(uintptr_t)) {
        uintptr_t value;
        std::memcpy(&value, stack + offset, sizeof(value));
        if (value >= module_base_ && value < module_base_ + module_size_) {
          return static_cast<uint32_t>(value - module_base_);
        }
      }
    }
    return 0xFFFFFFFFu;
  }

  void sample_loop() {
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT,
                               FALSE, main_thread_id_);
    if (!thread) {
      open_failed_ = true;
      return;
    }
    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL;
    while (!stop_.load(std::memory_order_relaxed)) {
      Sleep(period_ms_);
      if (stop_.load(std::memory_order_relaxed)) break;
      if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
        ++suspend_failures_;
        continue;
      }
      context.ContextFlags = CONTEXT_CONTROL;
      if (GetThreadContext(thread, &context)) {
        uint64_t progress = 0;
        if (progress_) {
          progress = progress_->load(std::memory_order_relaxed);
        }
        WindowTable& table =
            (*windows_)[static_cast<size_t>(
                std::min<uint64_t>(progress / kWindowInsns, kWindowCount - 1))];
        const uintptr_t rip = static_cast<uintptr_t>(context.Rip);
        uint32_t leaf_rva = 0;
        const ModuleRange* range = locate(rip, leaf_rva);
        const bool inside_xlle =
            rip >= module_base_ && rip < module_base_ + module_size_;
        record_leaf(table, range ? range->id : 0, leaf_rva);
        if (!inside_xlle && sym_initialized_) {
          // Blocking/copy time outside our code: attribute to the xlle frame.
          const uint32_t caller = first_xlle_caller(thread, context);
          record_pair(table, range ? range->id : 0, leaf_rva,
                      caller == 0xFFFFFFFFu ? 0u : caller);
        }
        ++samples_;
        ++table.samples;
      } else {
        ++context_failures_;
      }
      ResumeThread(thread);
    }
    CloseHandle(thread);
  }

  void report() {
    std::printf("[host-sampler] samples=%llu period_ms=%u module_base=0x%zx "
                "windows=%zu window_insns=%llu overflow=%llu "
                "suspend_failures=%llu context_failures=%llu open_failed=%d\n",
                static_cast<unsigned long long>(samples_), period_ms_,
                module_base_, kWindowCount,
                static_cast<unsigned long long>(kWindowInsns),
                static_cast<unsigned long long>(overflow_),
                static_cast<unsigned long long>(suspend_failures_),
                static_cast<unsigned long long>(context_failures_),
                open_failed_ ? 1 : 0);
    for (size_t window = 0; window < kWindowCount; ++window) {
      const WindowTable& table = (*windows_)[window];
      if (!table.samples) continue;
      auto module_name = [&](uint32_t id) -> const char* {
        return id < module_names_.size() ? module_names_[id].c_str()
                                         : "[unknown-module]";
      };
      std::vector<Bucket> entries;
      entries.reserve(table.used);
      for (const Bucket& bucket : table.buckets) {
        if (bucket.count) entries.push_back(bucket);
      }
      std::sort(entries.begin(), entries.end(),
                [](const Bucket& a, const Bucket& b) { return a.count > b.count; });
      std::printf("[host-sampler] window=%zu insns=%llu-%llu samples=%llu "
                  "no_module_leaves=%llu\n",
                  window,
                  static_cast<unsigned long long>(window * kWindowInsns),
                  static_cast<unsigned long long>((window + 1) * kWindowInsns),
                  static_cast<unsigned long long>(table.samples),
                  static_cast<unsigned long long>(table.leaf_outside_samples));
      const size_t limit = std::min(entries.size(), kMaxReportEntries);
      for (size_t i = 0; i < limit; ++i) {
        std::printf("[host-sampler] off=0x%08x count=%llu module=%s\n",
                    entries[i].rva,
                    static_cast<unsigned long long>(entries[i].count),
                    module_name(entries[i].module));
      }
      std::vector<PairBucket> pairs;
      pairs.reserve(table.pairs_used);
      for (const PairBucket& bucket : table.pairs) {
        if (bucket.count) pairs.push_back(bucket);
      }
      std::sort(pairs.begin(), pairs.end(),
                [](const PairBucket& a, const PairBucket& b) {
                  return a.count > b.count;
                });
      const size_t pair_limit = std::min(pairs.size(), kMaxPairEntries);
      for (size_t i = 0; i < pair_limit; ++i) {
        std::printf("[host-sampler] pair leaf-module=%s off=0x%08x "
                    "xlle-caller=0x%08x count=%llu\n",
                    module_name(pairs[i].leaf_module), pairs[i].leaf_rva,
                    pairs[i].caller_rva,
                    static_cast<unsigned long long>(pairs[i].count));
      }
    }
  }

  std::thread worker_;
  std::atomic<bool> stop_{false};
  const std::atomic<uint64_t>* progress_ = nullptr;
  HANDLE process_handle_ = nullptr;
  uint32_t period_ms_ = 2;
  uintptr_t module_base_ = 0;
  size_t module_size_ = 0;
  DWORD process_id_ = 0;
  DWORD main_thread_id_ = 0;
  bool sym_initialized_ = false;
  std::vector<ModuleRange> module_ranges_;
  std::vector<std::string> module_names_;
  uint64_t samples_ = 0;
  uint64_t overflow_ = 0;
  uint64_t suspend_failures_ = 0;
  uint64_t context_failures_ = 0;
  bool open_failed_ = false;
  std::unique_ptr<std::array<WindowTable, kWindowCount>> windows_;
};

}  // namespace xlle

#else  // !_WIN32

namespace xlle {

// Non-Windows stub: probe unavailable, binary stays portable.
class HostSampleProfiler {
 public:
  void set_progress_source(const std::atomic<uint64_t>*) {}
  void start(uint32_t) {}
  void stop_and_report() {
    std::printf("[host-sampler] unavailable on this platform\n");
  }
};

}  // namespace xlle

#endif
