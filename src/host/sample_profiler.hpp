// Opt-in host-side sampled profiler for xlle (Windows).
//
// Doctrine fit: this is an always-available observability probe, not a behavior
// change. It is OFF unless `--host-sample-profile <period_ms>` is passed; it
// never touches guest state, device state, or timing, so replay hashes are
// unaffected. It samples ONLY the main emulation thread at a fixed host period.
//
// Per guest-instruction window (1.5B instructions) it reports:
//   * the COMPLETE leaf table — every non-empty hash bucket, one `leaf` line
//     each. Nothing is truncated, so any consumer can compute exact shares.
//   * a human top-N summary whose percentages are computed over the window's
//     TRUE sample count, preceded by a `coverage` line stating what fraction of
//     the window the top-N actually covers.
//   * blocking-call attribution — when the leaf RIP is outside xlle.exe (a
//     kernel syscall stub, CRT copy routine, GL driver, or the JIT code cache)
//     a StackWalk64 pass finds the first xlle.exe caller frame, with a raw
//     stack scan as fallback; each pair records WHICH method attributed it.
//   * an `attribution` line exporting how many pairs each method resolved, the
//     mean depth at which it resolved them, and the unattributed count.
//   * a `jit` line: samples whose RIP fell in NO loaded module at all. That is
//     Unicorn/TCG generated code, and it is the first time the TCG share of
//     wall time is visible. It is NOT the same thing as `leaves_outside_xlle`,
//     which also counts ntdll/msvcrt/GL-ICD leaves.
//
// Rule 9 of docs/PERF-METHODOLOGY.md §2 — "instruments carry their own
// denominators and cost" — is implemented here by:
//   * exporting the true per-window sample count and the top-N coverage,
//   * exporting the ACHIEVED mean sample period (elapsed / samples) rather than
//     assuming the requested one was honoured, and naming the timer mechanism,
//   * stamping every profile with the running image's path, size and SHA-256 so
//     it can be symbolized later against the exact binary it came from
//     (tools/archive_profile_binary.py stores that binary next to the log;
//     tools/symbolize_host_profile.py refuses a mismatched exe).
//
// Output line grammar (all lines prefixed `[host-sampler] `):
//   samples=<n> period_ms=<req> module_base=0x<va> module_size=0x<n>
//       windows=<n> window_insns=<n> overflow=<n> suspend_failures=<n>
//       context_failures=<n> open_failed=<0|1> timer=<mode>
//       elapsed_ms=<f> mean_period_ms=<f> achieved_hz=<f>
//   binary sha256=<hex|selftest-failed|unavailable> size=<bytes> path=<utf8>
//   module id=<n> base=0x<va> size=0x<n> name=<utf8>
//   window=<w> insns=<a>-<b> samples=<n> leaves_outside_xlle=<n>
//       jit_samples=<n> unique_leaves=<n> unique_pairs=<n> span_ms=<f> hz=<f>
//   jit window=<w> samples=<n> pct=<f> lo=0x<va> hi=0x<va>
//   attribution window=<w> pairs=<n> walk=<n> walk_mean_depth=<f> scan=<n>
//       scan_mean_qwords=<f> unattributed=<n> unattributed_pct=<f>
//   leaf w=<w> m=<module id> off=0x<hex> n=<count>        <- COMPLETE table
//   coverage window=<w> top=<n> top_samples=<n> top_pct=<f>
//   top window=<w> rank=<n> count=<n> pct=<f> off=0x<hex> module=<name>
//   rest window=<w> entries=<n> count=<n> pct=<f>
//   pair leaf-module=<name> off=0x<hex> xlle-caller=0x<rva> count=<n>
//       method=<walk|scan|none>
// `off=` on a `leaf`/`top`/`pair` line is module-relative when m != 0 and the
// RAW 64-bit RIP when m == 0 (no module owns it).
//
// Knobs (env, so no CLI/main.cpp surface is added):
//   XLLE_HOST_SAMPLER_TOP=<n>          human top-N size (default 24)
//   XLLE_HOST_SAMPLER_FULL_TABLE=0     suppress the complete `leaf` table
//
// Known limits (stated, not hidden): single-threaded attribution only — GL
// driver worker threads are not sampled directly (they surface as main-thread
// waits); stack walks can fail across JIT frames, in which case the pair is
// reported with caller=0 and method=none.
#pragma once

#include <cstdint>
#include <cstdlib>
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

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#ifndef TIMER_ALL_ACCESS
#define TIMER_ALL_ACCESS 0x1F0003
#endif

namespace xlle {

class HostSampleProfiler {
 public:
  static constexpr size_t kTableSize = 1u << 15;      // power of two
  static constexpr size_t kPairTableSize = 1u << 13;  // power of two
  static constexpr size_t kDefaultReportEntries = 24;
  static constexpr size_t kMaxPairEntries = 16;
  static constexpr size_t kWindowCount = 8;
  static constexpr size_t kMaxExecRanges = 8;
  static constexpr size_t kStackScanBytes = 64 * 1024;
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
    resolve_main_stack_bounds();
    read_env_knobs();
    capture_binary_identity();
    snapshot_modules();
    QueryPerformanceFrequency(&qpc_frequency_);
    // fInvadeProcess=TRUE so dbghelp knows every loaded module and
    // SymFunctionTableAccess64 can hand StackWalk64 real .pdata unwind info.
    // With FALSE (the previous setting) dbghelp knew no modules, the AMD64
    // walker fell back to its leaf-frame heuristic on the very first frame, and
    // attribution failed for ~75% of blocking pairs. DEFERRED_LOADS keeps the
    // invade cheap: module records are built, symbol files are not read.
    SymSetOptions(SymGetOptions() | SYMOPT_DEFERRED_LOADS |
                  SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    sym_initialized_ = SymInitialize(process_handle_, nullptr, TRUE) == TRUE;
    // beads-xbox.1.14.7 review item: force the module list to be built HERE, on
    // the main thread, before the sampler thread exists. SymInitialize's invade
    // walks the loader list, but any module loaded after this point -- and, on
    // some dbghelp versions, the deferred records themselves -- is first
    // materialised by whichever thread next calls a Sym* API. That thread is
    // the sampler, and it calls Sym* while the target thread is SUSPENDED. If
    // the target holds the loader lock at that moment, dbghelp's module refresh
    // blocks on it and the process deadlocks. Refreshing on the main thread
    // while nothing is suspended removes that class of deadlock.
    if (sym_initialized_) SymRefreshModuleList(process_handle_);
    // Preallocated on the starting thread: the sampler thread never allocates.
    stack_scan_.assign(kStackScanBytes, 0);
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
  // --- Minimal SHA-256 (FIPS 180-4) -----------------------------------------
  // Inline so the profiler adds no link dependency. `self_test()` checks the
  // two published vectors at start(), so a transcription error can never emit a
  // plausible-but-wrong digest: the header says `selftest-failed` instead.
  struct Sha256 {
    uint32_t h[8] = {0};
    uint64_t length = 0;
    uint8_t block[64] = {0};
    size_t block_len = 0;

    static uint32_t rotr(uint32_t x, uint32_t n) {
      return (x >> n) | (x << (32u - n));
    }

    void init() {
      static const uint32_t kInit[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                        0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                        0x1f83d9abu, 0x5be0cd19u};
      std::memcpy(h, kInit, sizeof(h));
      length = 0;
      block_len = 0;
    }

    void transform(const uint8_t* chunk) {
      static const uint32_t kRound[64] = {
          0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
          0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
          0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
          0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
          0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
          0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
          0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
          0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
          0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
          0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
          0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
          0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
          0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
          0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
          0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
          0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
      uint32_t w[64];
      for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(chunk[i * 4 + 0]) << 24) |
               (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(chunk[i * 4 + 3]);
      }
      for (int i = 16; i < 64; ++i) {
        const uint32_t s0 =
            rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 =
            rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
      }
      uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
      uint32_t e = h[4], f = h[5], g = h[6], hv = h[7];
      for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = hv + S1 + ch + kRound[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = S0 + maj;
        hv = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d;
      h[4] += e; h[5] += f; h[6] += g; h[7] += hv;
    }

    void update(const uint8_t* data, size_t len) {
      length += static_cast<uint64_t>(len);
      while (len) {
        const size_t take = std::min(sizeof(block) - block_len, len);
        std::memcpy(block + block_len, data, take);
        block_len += take;
        data += take;
        len -= take;
        if (block_len == sizeof(block)) {
          transform(block);
          block_len = 0;
        }
      }
    }

    std::string final_hex() {
      const uint64_t bits = length * 8ull;
      const uint8_t one = 0x80;
      update(&one, 1);
      const uint8_t zero = 0;
      while (block_len != 56) update(&zero, 1);
      uint8_t tail[8];
      for (int i = 0; i < 8; ++i) {
        tail[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
      }
      update(tail, 8);
      char out[65];
      for (int i = 0; i < 8; ++i) {
        std::snprintf(out + i * 8, 9, "%08x", h[i]);
      }
      out[64] = '\0';
      return std::string(out);
    }

    static bool self_test() {
      Sha256 empty;
      empty.init();
      if (empty.final_hex() !=
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") {
        return false;
      }
      Sha256 abc;
      abc.init();
      abc.update(reinterpret_cast<const uint8_t*>("abc"), 3);
      return abc.final_hex() ==
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    }
  };

  struct Bucket {
    uint64_t addr;  // module-relative RVA, or the raw RIP when module == 0
    uint64_t count;
    uint32_t module;
  };
  // Leaf-outside-xlle sample paired with the first xlle.exe caller frame:
  // attributes blocking syscalls/copies/JIT to the xlle call site that issued
  // them. `method` records HOW the caller was found, so a consumer can weight
  // StackWalk64 results (sound) against raw-scan results (heuristic).
  struct PairBucket {
    uint64_t leaf_addr;
    uint64_t count;
    uint32_t leaf_module;
    uint32_t caller_rva;
    uint32_t method;  // 0 = none, 1 = StackWalk64, 2 = raw stack scan
  };
  struct WindowTable {
    std::array<Bucket, kTableSize> buckets{};
    std::array<PairBucket, kPairTableSize> pairs{};
    size_t used = 0;
    size_t pairs_used = 0;
    uint64_t samples = 0;
    // Leaves whose RIP was outside the xlle.exe image: ntdll, msvcrt, the GL
    // ICD, AND the JIT code cache. Formerly mislabelled `no_module_leaves`.
    uint64_t leaf_outside_samples = 0;
    // Leaves inside NO loaded module at all: Unicorn/TCG generated code.
    uint64_t jit_samples = 0;
    uint64_t jit_lo = 0;
    uint64_t jit_hi = 0;
    uint64_t attr_walk = 0;
    uint64_t attr_scan = 0;
    uint64_t attr_none = 0;
    uint64_t walk_depth_sum = 0;
    uint64_t scan_words_sum = 0;
    uint64_t first_qpc = 0;
    uint64_t last_qpc = 0;
  };
  struct ModuleRange {
    uintptr_t base;
    size_t size;
    uint32_t id;  // index into module_names_
  };
  struct ExecRange {
    uintptr_t begin;
    uintptr_t end;
  };
  // How the first xlle caller frame was found for one blocking sample.
  struct CallerAttribution {
    uint32_t rva = 0xFFFFFFFFu;
    uint32_t method = 0;  // 0 = none, 1 = StackWalk64, 2 = raw stack scan
    uint32_t depth = 0;   // walk: frames consumed; scan: qwords past RSP
  };

  void read_env_knobs() {
    if (const char* top = std::getenv("XLLE_HOST_SAMPLER_TOP")) {
      const long parsed = std::strtol(top, nullptr, 10);
      if (parsed > 0 && parsed < 100000) {
        report_entries_ = static_cast<size_t>(parsed);
      }
    }
    if (const char* full = std::getenv("XLLE_HOST_SAMPLER_FULL_TABLE")) {
      full_table_ = !(full[0] == '0' && full[1] == '\0');
    }
  }

  // Records the exact image this profile came from so it can be symbolized
  // later even after the build directory has moved on (PERF-METHODOLOGY §2.9).
  void capture_binary_identity() {
    std::vector<wchar_t> path(32768, 0);
    const DWORD length =
        GetModuleFileNameW(nullptr, path.data(),
                           static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
      exe_path_ = "[unavailable]";
      exe_sha256_ = "unavailable";
      return;
    }
    exe_path_ = to_utf8(path.data());
    if (!Sha256::self_test()) {
      exe_sha256_ = "selftest-failed";
      return;
    }
    exe_sha256_ = hash_file(path.data(), exe_size_);
  }

  static std::string to_utf8(const wchar_t* wide) {
    const int needed =
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return std::string();
    std::vector<char> buffer(static_cast<size_t>(needed), 0);
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer.data(), needed, nullptr,
                        nullptr);
    return std::string(buffer.data());
  }

  static std::string hash_file(const wchar_t* path, uint64_t& size_out) {
    size_out = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return "unavailable";
    Sha256 sha;
    sha.init();
    std::vector<uint8_t> buffer(1u << 16);
    for (;;) {
      DWORD read = 0;
      if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                    &read, nullptr)) {
        CloseHandle(file);
        return "read-error";
      }
      if (read == 0) break;
      sha.update(buffer.data(), read);
      size_out += read;
    }
    CloseHandle(file);
    return sha.final_hex();
  }

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
    resolve_exec_ranges();
  }

  // Parses the main image's section headers so the raw stack scan can reject
  // non-code xlle addresses (saved data pointers into .data/.rdata otherwise
  // masquerade as return sites). ALL executable sections are collected, not
  // just the first one named ".text": a MinGW link can place code in more than
  // one section, and a return address into any of them is a legitimate frame.
  void resolve_exec_ranges() {
    exec_range_count_ = 0;
    HMODULE base = reinterpret_cast<HMODULE>(module_base_);
    if (!base) return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<const uint8_t*>(base) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections;
         ++index, ++section) {
      if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
      if (exec_range_count_ >= kMaxExecRanges) break;
      const uintptr_t begin =
          module_base_ + static_cast<uintptr_t>(section->VirtualAddress);
      size_t extent = section->Misc.VirtualSize;
      if (extent == 0) extent = section->SizeOfRawData;
      exec_ranges_[exec_range_count_].begin = begin;
      exec_ranges_[exec_range_count_].end = begin + extent;
      ++exec_range_count_;
    }
  }

  // Caches the main thread's stack top so the per-sample raw scan can bound
  // itself without an address-space query while that thread is suspended.
  // MUST be called from the main thread (start() is).
  void resolve_main_stack_bounds() {
    stack_high_ = 0;
    if (HMODULE kernel = GetModuleHandleW(L"kernel32.dll")) {
      using GetStackLimitsFn = void(WINAPI*)(PULONG_PTR, PULONG_PTR);
      auto limits = reinterpret_cast<GetStackLimitsFn>(
          GetProcAddress(kernel, "GetCurrentThreadStackLimits"));
      if (limits) {
        ULONG_PTR low = 0, high = 0;
        limits(&low, &high);
        stack_high_ = static_cast<uintptr_t>(high);
        return;
      }
    }
    // Pre-Win8 fallback: the committed region containing a local is the stack.
    MEMORY_BASIC_INFORMATION info{};
    int probe = 0;
    if (VirtualQuery(&probe, &info, sizeof(info)) == sizeof(info)) {
      stack_high_ =
          reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
  }

  bool in_xlle_code(uintptr_t value) const {
    for (size_t index = 0; index < exec_range_count_; ++index) {
      if (value >= exec_ranges_[index].begin &&
          value < exec_ranges_[index].end) {
        return true;
      }
    }
    return false;
  }

  // Returns the matched range or nullptr; addr receives rip relative to base
  // (or the raw rip when no module matches).
  const ModuleRange* locate(uintptr_t rip, uint64_t& addr) const {
    size_t low = 0, high = module_ranges_.size();
    while (low < high) {
      const size_t mid = (low + high) / 2;
      if (rip < module_ranges_[mid].base) {
        high = mid;
      } else if (rip >= module_ranges_[mid].base + module_ranges_[mid].size) {
        low = mid + 1;
      } else {
        addr = static_cast<uint64_t>(rip - module_ranges_[mid].base);
        return &module_ranges_[mid];
      }
    }
    // No module owns this RIP: keep the FULL address, not a 32-bit truncation.
    // These are the JIT/TCG code-cache samples and their absolute placement is
    // the only handle a later analysis has on them.
    addr = static_cast<uint64_t>(rip);
    return nullptr;
  }

  void record_leaf(WindowTable& table, uint32_t module_id, uint64_t addr) {
    uint64_t key = addr ^ (static_cast<uint64_t>(module_id) *
                           0x9E3779B97F4A7C15ull);
    size_t slot = static_cast<size_t>((key * 0xff51afd7ed558ccdull) >> 48) &
                  (kTableSize - 1);
    for (size_t probes = 0; probes < 64; ++probes) {
      Bucket& bucket = table.buckets[slot];
      if (bucket.count == 0) {
        bucket.addr = addr;
        bucket.module = module_id;
        bucket.count = 1;
        ++table.used;
        return;
      }
      if (bucket.addr == addr && bucket.module == module_id) {
        ++bucket.count;
        return;
      }
      slot = (slot + 1) & (kTableSize - 1);
    }
    ++overflow_;  // probe budget exhausted: drop rather than allocate
  }

  void record_pair(WindowTable& table, uint32_t leaf_module, uint64_t leaf_addr,
                   uint32_t caller_rva, uint32_t method) {
    ++table.leaf_outside_samples;
    uint64_t key = leaf_addr ^ (static_cast<uint64_t>(leaf_module) << 48) ^
                   (static_cast<uint64_t>(caller_rva) * 0x9E3779B97F4A7C15ull) ^
                   (static_cast<uint64_t>(method) << 61);
    size_t slot = static_cast<size_t>((key * 0xff51afd7ed558ccdull) >> 48) &
                  (kPairTableSize - 1);
    for (size_t probes = 0; probes < 32; ++probes) {
      PairBucket& bucket = table.pairs[slot];
      if (bucket.count == 0) {
        bucket.leaf_addr = leaf_addr;
        bucket.leaf_module = leaf_module;
        bucket.caller_rva = caller_rva;
        bucket.method = method;
        bucket.count = 1;
        ++table.pairs_used;
        return;
      }
      if (bucket.leaf_addr == leaf_addr && bucket.leaf_module == leaf_module &&
          bucket.caller_rva == caller_rva && bucket.method == method) {
        ++bucket.count;
        return;
      }
      slot = (slot + 1) & (kPairTableSize - 1);
    }
    ++overflow_;
  }

  // Finds the first stack frame inside the xlle module and reports HOW it was
  // found. Two independent mechanisms, tried in order:
  //   1. StackWalk64 over dbghelp's .pdata unwind info (sound; needs the module
  //      list, which start() now loads via SymInitialize(..., TRUE)).
  //   2. A raw scan of the thread stack for the first qword that lands in an
  //      xlle executable section (heuristic: a live saved return address is the
  //      common case, but a stale one from an earlier deeper call is possible;
  //      that is why the method is reported per pair rather than merged).
  CallerAttribution find_xlle_caller(HANDLE thread, const CONTEXT& context) {
    CallerAttribution out;
    if (sym_initialized_) {
      STACKFRAME64 frame{};
      frame.AddrPC.Offset = context.Rip;
      frame.AddrPC.Mode = AddrModeFlat;
      frame.AddrFrame.Offset = context.Rsp;
      frame.AddrFrame.Mode = AddrModeFlat;
      frame.AddrStack.Offset = context.Rsp;
      frame.AddrStack.Mode = AddrModeFlat;
      CONTEXT walk_context = context;
      for (unsigned depth = 0; depth < 64; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process_handle_, thread,
                         &frame, &walk_context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64,
                         nullptr)) {
          break;
        }
        const uintptr_t pc = static_cast<uintptr_t>(frame.AddrPC.Offset);
        if (!pc) break;
        if (pc >= module_base_ && pc < module_base_ + module_size_) {
          out.rva = static_cast<uint32_t>(pc - module_base_);
          out.method = 1;
          out.depth = depth;
          return out;
        }
      }
    }
    // Meta-unwind failed (GL ICD and JIT frames often break it): fall back to a
    // raw stack scan for the nearest value that is an xlle code address.
    const uintptr_t rsp = static_cast<uintptr_t>(context.Rsp);
    if (!exec_range_count_ || !rsp || stack_scan_.empty()) return out;
    size_t limit = stack_scan_.size();
    // Bound the read by the main thread's stack top so the scan never runs off
    // the end of the stack. The bound is resolved ONCE in start(), on the main
    // thread: querying the address space from here would mean calling
    // VirtualQueryEx while the target thread is suspended, and that thread can
    // be holding the address-space lock inside the JIT's own VirtualAlloc.
    // The previous code read a fixed 16 KiB and required ReadProcessMemory to
    // return TRUE; near the top of the stack that call fails wholesale and the
    // scan was skipped entirely, which is a large part of why 75% of blocking
    // pairs had no caller.
    if (stack_high_) {
      if (stack_high_ <= rsp) return out;  // RSP outside the main-thread stack
      limit = std::min<size_t>(limit, static_cast<size_t>(stack_high_ - rsp));
    }
    SIZE_T read = 0;
    // A partial copy returns FALSE but still reports the bytes transferred, so
    // `read` is used regardless of the return value.
    ReadProcessMemory(process_handle_, reinterpret_cast<LPCVOID>(rsp),
                      stack_scan_.data(), limit, &read);
    if (static_cast<size_t>(read) > limit) read = limit;
    for (SIZE_T offset = 0; offset + sizeof(uintptr_t) <= read;
         offset += sizeof(uintptr_t)) {
      uintptr_t value;
      std::memcpy(&value, stack_scan_.data() + offset, sizeof(value));
      if (in_xlle_code(value)) {
        out.rva = static_cast<uint32_t>(value - module_base_);
        out.method = 2;
        out.depth = static_cast<uint32_t>(offset / sizeof(uintptr_t));
        return out;
      }
    }
    return out;
  }

  // --- Sample pacing --------------------------------------------------------
  // Sleep(period_ms) alone is quantised to the system timer tick (~15.6 ms by
  // default), so a requested 2 ms period silently became ~15 ms. Preferred fix
  // is a high-resolution waitable timer: sub-millisecond wakeups WITHOUT
  // timeBeginPeriod's process-wide side effect, which would also change the
  // timing of the emulation thread we are measuring. Both entry points are
  // resolved dynamically, so this adds no link dependency and no minimum
  // _WIN32_WINNT. If the owner prefers a static winmm link instead, the CMake
  // line is: target_link_libraries(xlle PRIVATE psapi dbghelp winmm)
  using CreateWaitableTimerExWFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES,
                                                   LPCWSTR, DWORD, DWORD);
  using TimePeriodFn = UINT(WINAPI*)(UINT);

  void begin_pacing() {
    if (HMODULE kernel = GetModuleHandleW(L"kernel32.dll")) {
      auto create = reinterpret_cast<CreateWaitableTimerExWFn>(
          GetProcAddress(kernel, "CreateWaitableTimerExW"));
      if (create) {
        hires_timer_ = create(nullptr, nullptr,
                              CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                              TIMER_ALL_ACCESS);
        if (hires_timer_) {
          timer_mode_ = "waitable-hires";
          return;
        }
      }
    }
    winmm_ = LoadLibraryW(L"winmm.dll");
    if (winmm_) {
      time_begin_period_ =
          reinterpret_cast<TimePeriodFn>(GetProcAddress(winmm_,
                                                        "timeBeginPeriod"));
      time_end_period_ =
          reinterpret_cast<TimePeriodFn>(GetProcAddress(winmm_,
                                                        "timeEndPeriod"));
      if (time_begin_period_ && time_end_period_ &&
          time_begin_period_(1) == 0) {
        timer_mode_ = "winmm-period1";
        return;
      }
      time_begin_period_ = nullptr;
      time_end_period_ = nullptr;
      FreeLibrary(winmm_);
      winmm_ = nullptr;
    }
    timer_mode_ = "sleep";
  }

  void wait_period() {
    if (hires_timer_) {
      LARGE_INTEGER due;
      due.QuadPart = -static_cast<LONGLONG>(period_ms_) * 10000LL;
      if (SetWaitableTimer(hires_timer_, &due, 0, nullptr, nullptr, FALSE)) {
        WaitForSingleObject(hires_timer_, INFINITE);
        return;
      }
    }
    Sleep(period_ms_);
  }

  void end_pacing() {
    if (hires_timer_) {
      CloseHandle(hires_timer_);
      hires_timer_ = nullptr;
    }
    if (time_end_period_) {
      time_end_period_(1);
      time_end_period_ = nullptr;
      time_begin_period_ = nullptr;
    }
    if (winmm_) {
      FreeLibrary(winmm_);
      winmm_ = nullptr;
    }
  }

  static uint64_t now_qpc() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<uint64_t>(value.QuadPart);
  }

  double qpc_to_ms(uint64_t ticks) const {
    if (qpc_frequency_.QuadPart <= 0) return 0.0;
    return 1000.0 * static_cast<double>(ticks) /
           static_cast<double>(qpc_frequency_.QuadPart);
  }

  void sample_loop() {
    begin_pacing();
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT,
                               FALSE, main_thread_id_);
    if (!thread) {
      open_failed_ = true;
      end_pacing();
      return;
    }
    CONTEXT context{};
    start_qpc_ = now_qpc();
    while (!stop_.load(std::memory_order_relaxed)) {
      wait_period();
      if (stop_.load(std::memory_order_relaxed)) break;
      // Timestamp BEFORE the suspend so the clock read never lengthens the
      // window the emulation thread is held for.
      const uint64_t stamp = now_qpc();
      if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
        ++suspend_failures_;
        continue;
      }
      // CONTEXT_FULL (on AMD64: control + integer + floating point) rather than
      // CONTEXT_CONTROL alone. RtlVirtualUnwind, reached through StackWalk64,
      // reads and rewrites the non-volatile integer registers; with only
      // CONTEXT_CONTROL those carried stale values from the previous sample,
      // because `context` is reused across iterations.
      context.ContextFlags = CONTEXT_FULL;
      if (GetThreadContext(thread, &context)) {
        uint64_t progress = 0;
        if (progress_) {
          progress = progress_->load(std::memory_order_relaxed);
        }
        WindowTable& table =
            (*windows_)[static_cast<size_t>(
                std::min<uint64_t>(progress / kWindowInsns, kWindowCount - 1))];
        const uintptr_t rip = static_cast<uintptr_t>(context.Rip);
        uint64_t leaf_addr = 0;
        const ModuleRange* range = locate(rip, leaf_addr);
        const bool inside_xlle =
            rip >= module_base_ && rip < module_base_ + module_size_;
        record_leaf(table, range ? range->id : 0, leaf_addr);
        if (!range) {
          // No loaded module owns this RIP: Unicorn/TCG generated code.
          ++table.jit_samples;
          if (!table.jit_lo || rip < table.jit_lo) table.jit_lo = rip;
          if (rip > table.jit_hi) table.jit_hi = rip;
        }
        if (!inside_xlle) {
          // Blocking/copy/JIT time outside our code: attribute to the xlle
          // frame that issued it. Attempted even when dbghelp failed to
          // initialise, because the raw stack scan does not need dbghelp.
          const CallerAttribution caller = find_xlle_caller(thread, context);
          record_pair(table, range ? range->id : 0, leaf_addr,
                      caller.rva == 0xFFFFFFFFu ? 0u : caller.rva,
                      caller.method);
          if (caller.method == 1) {
            ++table.attr_walk;
            table.walk_depth_sum += caller.depth;
          } else if (caller.method == 2) {
            ++table.attr_scan;
            table.scan_words_sum += caller.depth;
          } else {
            ++table.attr_none;
          }
        }
        ++samples_;
        ++table.samples;
        if (!table.first_qpc) table.first_qpc = stamp;
        table.last_qpc = stamp;
      } else {
        ++context_failures_;
      }
      ResumeThread(thread);
    }
    stop_qpc_ = now_qpc();
    CloseHandle(thread);
    end_pacing();
  }

  static const char* method_name(uint32_t method) {
    if (method == 1) return "walk";
    if (method == 2) return "scan";
    return "none";
  }

  void report() {
    const double elapsed_ms =
        stop_qpc_ > start_qpc_ ? qpc_to_ms(stop_qpc_ - start_qpc_) : 0.0;
    const double mean_period_ms =
        samples_ ? elapsed_ms / static_cast<double>(samples_) : 0.0;
    const double achieved_hz =
        elapsed_ms > 0.0 ? 1000.0 * static_cast<double>(samples_) / elapsed_ms
                         : 0.0;
    std::printf("[host-sampler] samples=%llu period_ms=%u module_base=0x%zx "
                "module_size=0x%zx windows=%zu window_insns=%llu "
                "overflow=%llu suspend_failures=%llu context_failures=%llu "
                "open_failed=%d timer=%s elapsed_ms=%.3f mean_period_ms=%.3f "
                "achieved_hz=%.1f\n",
                static_cast<unsigned long long>(samples_), period_ms_,
                module_base_, module_size_, kWindowCount,
                static_cast<unsigned long long>(kWindowInsns),
                static_cast<unsigned long long>(overflow_),
                static_cast<unsigned long long>(suspend_failures_),
                static_cast<unsigned long long>(context_failures_),
                open_failed_ ? 1 : 0, timer_mode_, elapsed_ms, mean_period_ms,
                achieved_hz);
    // Provenance: path last, because it is the only field that can contain
    // spaces or '='.
    std::printf("[host-sampler] binary sha256=%s size=%llu path=%s\n",
                exe_sha256_.c_str(),
                static_cast<unsigned long long>(exe_size_), exe_path_.c_str());
    std::printf("[host-sampler] module id=0 base=0x0 size=0x0 name=%s\n",
                module_names_.empty() ? "[no-module]"
                                      : module_names_[0].c_str());
    for (const ModuleRange& range : module_ranges_) {
      std::printf("[host-sampler] module id=%u base=0x%zx size=0x%zx name=%s\n",
                  range.id, range.base, range.size,
                  range.id < module_names_.size()
                      ? module_names_[range.id].c_str()
                      : "[unknown-module]");
    }
    for (size_t window = 0; window < kWindowCount; ++window) {
      const WindowTable& table = (*windows_)[window];
      if (!table.samples) continue;
      auto module_name = [&](uint32_t id) -> const char* {
        return id < module_names_.size() ? module_names_[id].c_str()
                                         : "[unknown-module]";
      };
      const double window_samples = static_cast<double>(table.samples);
      std::vector<Bucket> entries;
      entries.reserve(table.used);
      for (const Bucket& bucket : table.buckets) {
        if (bucket.count) entries.push_back(bucket);
      }
      std::sort(entries.begin(), entries.end(),
                [](const Bucket& a, const Bucket& b) {
                  return a.count > b.count;
                });
      std::vector<PairBucket> pairs;
      pairs.reserve(table.pairs_used);
      for (const PairBucket& bucket : table.pairs) {
        if (bucket.count) pairs.push_back(bucket);
      }
      std::sort(pairs.begin(), pairs.end(),
                [](const PairBucket& a, const PairBucket& b) {
                  return a.count > b.count;
                });
      const double span_ms = table.last_qpc > table.first_qpc
                                 ? qpc_to_ms(table.last_qpc - table.first_qpc)
                                 : 0.0;
      const double window_hz =
          (span_ms > 0.0 && table.samples > 1)
              ? 1000.0 * static_cast<double>(table.samples - 1) / span_ms
              : 0.0;
      std::printf("[host-sampler] window=%zu insns=%llu-%llu samples=%llu "
                  "leaves_outside_xlle=%llu jit_samples=%llu "
                  "unique_leaves=%zu unique_pairs=%zu span_ms=%.3f hz=%.1f\n",
                  window,
                  static_cast<unsigned long long>(window * kWindowInsns),
                  static_cast<unsigned long long>((window + 1) * kWindowInsns),
                  static_cast<unsigned long long>(table.samples),
                  static_cast<unsigned long long>(table.leaf_outside_samples),
                  static_cast<unsigned long long>(table.jit_samples),
                  entries.size(), pairs.size(), span_ms, window_hz);
      std::printf("[host-sampler] jit window=%zu samples=%llu pct=%.2f "
                  "lo=0x%llx hi=0x%llx\n",
                  window,
                  static_cast<unsigned long long>(table.jit_samples),
                  100.0 * static_cast<double>(table.jit_samples) /
                      window_samples,
                  static_cast<unsigned long long>(table.jit_lo),
                  static_cast<unsigned long long>(table.jit_hi));
      const uint64_t attributed_total =
          table.attr_walk + table.attr_scan + table.attr_none;
      std::printf("[host-sampler] attribution window=%zu pairs=%llu walk=%llu "
                  "walk_mean_depth=%.2f scan=%llu scan_mean_qwords=%.2f "
                  "unattributed=%llu unattributed_pct=%.1f\n",
                  window,
                  static_cast<unsigned long long>(attributed_total),
                  static_cast<unsigned long long>(table.attr_walk),
                  table.attr_walk ? static_cast<double>(table.walk_depth_sum) /
                                        static_cast<double>(table.attr_walk)
                                  : 0.0,
                  static_cast<unsigned long long>(table.attr_scan),
                  table.attr_scan ? static_cast<double>(table.scan_words_sum) /
                                        static_cast<double>(table.attr_scan)
                                  : 0.0,
                  static_cast<unsigned long long>(table.attr_none),
                  attributed_total
                      ? 100.0 * static_cast<double>(table.attr_none) /
                            static_cast<double>(attributed_total)
                      : 0.0);
      // COMPLETE leaf table: every non-empty bucket, machine-parsable. This is
      // the denominator-safe surface; the top-N block below is for humans only.
      if (full_table_) {
        for (const Bucket& bucket : entries) {
          std::printf("[host-sampler] leaf w=%zu m=%u off=0x%08llx n=%llu\n",
                      window, bucket.module,
                      static_cast<unsigned long long>(bucket.addr),
                      static_cast<unsigned long long>(bucket.count));
        }
      }
      const size_t limit = std::min(entries.size(), report_entries_);
      uint64_t top_samples = 0;
      for (size_t i = 0; i < limit; ++i) top_samples += entries[i].count;
      std::printf("[host-sampler] coverage window=%zu top=%zu top_samples=%llu "
                  "top_pct=%.2f\n",
                  window, limit,
                  static_cast<unsigned long long>(top_samples),
                  100.0 * static_cast<double>(top_samples) / window_samples);
      for (size_t i = 0; i < limit; ++i) {
        // pct is over the window's TRUE sample count, never over the printed
        // subtotal. That inversion is what inflated the 2026-08-21 report ~2.2x.
        std::printf("[host-sampler] top window=%zu rank=%zu count=%llu "
                    "pct=%.2f off=0x%08llx module=%s\n",
                    window, i,
                    static_cast<unsigned long long>(entries[i].count),
                    100.0 * static_cast<double>(entries[i].count) /
                        window_samples,
                    static_cast<unsigned long long>(entries[i].addr),
                    module_name(entries[i].module));
      }
      if (entries.size() > limit) {
        const uint64_t rest = table.samples - top_samples;
        std::printf("[host-sampler] rest window=%zu entries=%zu count=%llu "
                    "pct=%.2f\n",
                    window, entries.size() - limit,
                    static_cast<unsigned long long>(rest),
                    100.0 * static_cast<double>(rest) / window_samples);
      }
      const size_t pair_limit = std::min(pairs.size(), kMaxPairEntries);
      for (size_t i = 0; i < pair_limit; ++i) {
        std::printf("[host-sampler] pair leaf-module=%s off=0x%08llx "
                    "xlle-caller=0x%08x count=%llu method=%s\n",
                    module_name(pairs[i].leaf_module),
                    static_cast<unsigned long long>(pairs[i].leaf_addr),
                    pairs[i].caller_rva,
                    static_cast<unsigned long long>(pairs[i].count),
                    method_name(pairs[i].method));
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
  size_t report_entries_ = kDefaultReportEntries;
  bool full_table_ = true;
  std::array<ExecRange, kMaxExecRanges> exec_ranges_{};
  size_t exec_range_count_ = 0;
  uintptr_t stack_high_ = 0;  // main thread's stack top, cached in start()
  std::vector<uint8_t> stack_scan_;
  std::vector<ModuleRange> module_ranges_;
  std::vector<std::string> module_names_;
  std::string exe_path_ = "[unavailable]";
  std::string exe_sha256_ = "unavailable";
  uint64_t exe_size_ = 0;
  LARGE_INTEGER qpc_frequency_{};
  uint64_t start_qpc_ = 0;
  uint64_t stop_qpc_ = 0;
  HANDLE hires_timer_ = nullptr;
  HMODULE winmm_ = nullptr;
  TimePeriodFn time_begin_period_ = nullptr;
  TimePeriodFn time_end_period_ = nullptr;
  const char* timer_mode_ = "sleep";
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
