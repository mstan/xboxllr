// xlle — A0 entry point.
//
// Deliverable (ROADMAP A0): the host runs and the chosen CPU floor executes instruction
// fixtures under ring instrumentation, with NO Xbox devices attached. This wires the
// observability skeleton (rings + JSONL + TCP debug server) to the Unicorn CPU floor and
// runs the fixture spike, fault-stopping loudly on any mismatch.
#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

#include "cpu/unicorn_floor.hpp"
#include "host/ethernet_udp.hpp"
#include "host/nv2a_opengl_renderer.hpp"
#include "host/sdl_shell.hpp"
#include "host/xid_script.hpp"
#include "machine/aci.hpp"
#include "machine/apu.hpp"
#include "machine/machine.hpp"
#include "machine/perf_accounting.hpp"
#include "obs/debug_server.hpp"
#include "spike/fixtures.hpp"
#include "xlle/jsonl.hpp"
#include "xlle/ring.hpp"

using namespace xlle;

namespace {

Ring<RetiredInsn> g_retired(1u << 16);
Ring<MemEvent> g_mem(1u << 16);
constexpr uint64_t kMinFrameSeriesInterval = 1000000;

struct HostMemoryUsage {
    uint64_t working_set = 0;
    uint64_t private_commit = 0;
};

struct HostHeapUsage {
    uint64_t allocated = 0;
    uint64_t committed = 0;
    uint64_t reserved = 0;
    uint64_t heaps = 0;
};

#if defined(_WIN32)
HANDLE g_host_memory_job = nullptr;
#endif

bool query_host_memory(HostMemoryUsage& usage) {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters))) {
        return false;
    }
    usage.working_set = static_cast<uint64_t>(counters.WorkingSetSize);
    usage.private_commit = static_cast<uint64_t>(counters.PrivateUsage);
    return true;
#else
    (void)usage;
    return false;
#endif
}

bool query_host_heaps(HostHeapUsage& usage) {
#if defined(_WIN32)
    const DWORD count = GetProcessHeaps(0, nullptr);
    if (!count) return false;
    std::vector<HANDLE> heaps(count);
    const DWORD returned = GetProcessHeaps(count, heaps.data());
    if (!returned || returned > count) return false;
    for (DWORD i = 0; i < returned; ++i) {
        HEAP_SUMMARY summary{};
        summary.cb = sizeof(summary);
        if (!HeapSummary(heaps[i], 0, &summary)) return false;
        usage.allocated += static_cast<uint64_t>(summary.cbAllocated);
        usage.committed += static_cast<uint64_t>(summary.cbCommitted);
        usage.reserved += static_cast<uint64_t>(summary.cbReserved);
    }
    usage.heaps = returned;
    return true;
#else
    (void)usage;
    return false;
#endif
}

bool install_host_memory_limit(uint64_t bytes, std::string& error) {
#if defined(_WIN32)
    HostMemoryUsage usage;
    if (!query_host_memory(usage)) {
        error = "GetProcessMemoryInfo failed before installing memory limit";
        return false;
    }
    if (usage.private_commit >= bytes) {
        char buf[192];
        std::snprintf(
            buf, sizeof(buf),
            "current private commit %llu MiB already meets/exceeds limit %llu MiB",
            static_cast<unsigned long long>(usage.private_commit / (1024ull * 1024ull)),
            static_cast<unsigned long long>(bytes / (1024ull * 1024ull)));
        error = buf;
        return false;
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        error = "CreateJobObjectW failed with Windows error " +
                std::to_string(GetLastError());
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(bytes);
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
        error = "SetInformationJobObject failed with Windows error " +
                std::to_string(GetLastError());
        CloseHandle(job);
        return false;
    }
    if (!AssignProcessToJobObject(job, GetCurrentProcess())) {
        error = "AssignProcessToJobObject failed with Windows error " +
                std::to_string(GetLastError());
        CloseHandle(job);
        return false;
    }
    g_host_memory_job = job;  // Process-lifetime handle: keeps the hard limit active.
    return true;
#else
    (void)bytes;
    error = "host memory limits are unsupported on this platform";
    return false;
#endif
}

uint32_t reg_field(const X86Regs& r, int i) {
    switch (i) {
        case 0: return r.eax; case 1: return r.ecx; case 2: return r.edx; case 3: return r.ebx;
        case 4: return r.esp; case 5: return r.ebp; case 6: return r.esi; case 7: return r.edi;
        case 8: return r.eip; default: return 0;
    }
}
const char* reg_name(int i) {
    static const char* n[] = {"eax","ecx","edx","ebx","esp","ebp","esi","edi","eip"};
    return n[i];
}

bool compare(const Fixture& f, const X86Regs& got, std::string& diff) {
    bool ok = true;
    for (int i = 0; i < 9; ++i) {
        if (!(f.reg_mask & (1u << i))) continue;
        uint32_t g = reg_field(got, i), e = reg_field(f.expect, i);
        if (g != e) {
            char b[96];
            std::snprintf(b, sizeof(b), " %s=0x%08x(exp 0x%08x)", reg_name(i), g, e);
            diff += b;
            ok = false;
        }
    }
    if (f.eflags_mask) {
        uint32_t d = (got.eflags ^ f.expect.eflags) & f.eflags_mask;
        if (d) {
            char b[96];
            std::snprintf(b, sizeof(b), " eflags-diff(masked)=0x%08x", d);
            diff += b;
            ok = false;
        }
    }
    return ok;
}

// Run one fixture on a fresh CPU floor; feed the global rings via the floor's callbacks.
bool run_fixture(const Fixture& f) {
    UnicornFloor uf;
    if (!uf.ok()) {
        std::printf("FAIL %-14s : could not open unicorn x86-32\n", f.name);
        return false;
    }
    uf.on_retired([](const RetiredInsn& e) { g_retired.push(e); });
    uf.on_mem([](const MemEvent& e) { g_mem.push(e); });

    if (!uf.map_mem(kCodeBase, kMapSize) ||
        !uf.write_mem(kCodeBase, f.code.data(), f.code.size())) {
        std::printf("FAIL %-14s : map/write failed\n", f.name);
        return false;
    }
    uf.set_regs(f.init);
    for (const auto& rb : f.ext_init) uf.write_reg(rb.reg, rb.bytes.data(), rb.bytes.size());

    std::string err;
    uint64_t n = uf.run(kCodeBase, f.insns, err);
    if (!err.empty()) {
        std::printf("FAIL %-14s : %s\n", f.name, err.c_str());
        return false;
    }

    X86Regs got = uf.get_regs();
    std::string diff;
    bool ok = compare(f, got, diff);

    if (ok && f.has_mem) {
        uint32_t v = 0;
        uf.read_mem(f.mem_addr, &v, sizeof(v));
        if (v != f.mem_expect) {
            char b[96];
            std::snprintf(b, sizeof(b), " mem[0x%x]=0x%08x(exp 0x%08x)", f.mem_addr, v,
                          f.mem_expect);
            diff += b;
            ok = false;
        }
    }

    for (const auto& rb : f.ext_check) {
        // Read into a max-width (XMM = 16B) buffer so Unicorn can never overflow us,
        // whatever size it writes for a given register; compare only the expected span.
        uint8_t raw[16] = {0};
        if (!uf.read_reg(rb.reg, raw, sizeof(raw))) {
            diff += " ext-reg-read-failed";
            ok = false;
            continue;
        }
        std::vector<uint8_t> got_b(raw, raw + rb.bytes.size());
        if (got_b != rb.bytes) {
            diff += " ext-reg";
            char h[8];
            diff += "(got ";
            for (auto x : got_b) { std::snprintf(h, sizeof(h), "%02x", x); diff += h; }
            diff += ")";
            ok = false;
        }
    }

    std::printf("%s %-14s : %llu insn%s%s\n", ok ? "PASS" : "FAIL", f.name,
                (unsigned long long)n, n == 1 ? " " : "s", ok ? "" : diff.c_str());
    return ok;
}

std::string dump_retired(size_t n) {
    std::vector<RetiredInsn> w;
    g_retired.window(n, w);
    std::string out;
    for (auto& e : w)
        out += JsonLine().str("ring", "retired").u("seq", e.seq).hex("eip", e.eip)
                   .u("size", e.size).line();
    return out;
}
std::string dump_mem(size_t n) {
    std::vector<MemEvent> w;
    g_mem.window(n, w);
    std::string out;
    for (auto& e : w)
        out += JsonLine().str("ring", "mem").u("seq", e.seq).b("write", e.write)
                   .hex("addr", e.addr).u("size", e.size).hex("value", e.value).line();
    return out;
}

uint64_t frame_hash(const std::vector<uint8_t>& rgba) {
    uint64_t hash = 1469598103934665603ull;
    for (uint8_t byte : rgba) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool write_frame_ppm(const char* path, const std::vector<uint8_t>& rgba,
                     uint32_t width, uint32_t height) {
    if (!path || rgba.size() != static_cast<size_t>(width) * height * 4) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (size_t i = 0; i < rgba.size(); i += 4) {
        out.put(static_cast<char>(rgba[i + 0]));
        out.put(static_cast<char>(rgba[i + 1]));
        out.put(static_cast<char>(rgba[i + 2]));
    }
    return out.good();
}

bool parse_u32_arg(const char* text, uint32_t& value) {
    if (!text || !text[0] || text[0] == '-' || text[0] == '+' ||
        std::isspace(static_cast<unsigned char>(text[0])))
        return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text, &end, 0);
    if (errno == ERANGE || !end || *end != '\0' || parsed > 0xffffffffull)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool parse_u64_arg(const char* text, uint64_t& value) {
    if (!text || !text[0] || text[0] == '-' || text[0] == '+' ||
        std::isspace(static_cast<unsigned char>(text[0])))
        return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text, &end, 0);
    if (errno == ERANGE || !end || *end != '\0') return false;
    value = static_cast<uint64_t>(parsed);
    return true;
}

bool capture_argb32_surface(XboxMachine& mach, uint32_t address, uint32_t pitch,
                            uint32_t width, uint32_t height,
                            std::vector<uint8_t>& rgba, std::string& error) {
    if (!width || !height || width > 2048 || height > 2048) {
        error = "surface dimensions are unset or too large";
        return false;
    }
    const uint64_t row_bytes = static_cast<uint64_t>(width) * 4ull;
    if (pitch < row_bytes) {
        error = "surface pitch is smaller than the visible row";
        return false;
    }
    const uint64_t source_size =
        static_cast<uint64_t>(pitch) * (height - 1) + row_bytes;
    if (static_cast<uint64_t>(address) + source_size > 64ull * 1024ull * 1024ull) {
        error = "surface exceeds unified RAM";
        return false;
    }
    std::vector<uint8_t> source(static_cast<size_t>(source_size));
    if (!mach.read(address, source.data(), source.size())) {
        error = "surface guest-memory read failed";
        return false;
    }
    rgba.resize(static_cast<size_t>(width) * height * 4ull);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t source_offset = static_cast<size_t>(y) * pitch + x * 4u;
            const size_t dest_offset = (static_cast<size_t>(y) * width + x) * 4u;
            rgba[dest_offset + 0] = source[source_offset + 2];
            rgba[dest_offset + 1] = source[source_offset + 1];
            rgba[dest_offset + 2] = source[source_offset + 0];
            rgba[dest_offset + 3] = 0xff;
        }
    }
    error.clear();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    bool serve = false, window = false, interactive = false;
    bool test_synthetic_controller_hub = false;
    bool test_synthetic_controller_hub_input_down = false;
    bool test_synthetic_controller_hub_input_down_a = false;
    bool reduced_retire_diag = false;
    bool perf_accounting = false;
    bool net_disconnected = false;
    bool pfifo_clear_trace = false;
    const char* nv2a_renderer_name = "software";
    uint32_t pfifo_clear_trace_target = 0;
    const char* boot_flash = nullptr;
    const char* run2bl = nullptr;
    const char* hdd = nullptr;
    const char* eeprom = nullptr;
    const char* frame_dump = nullptr;
    const char* debug_surface_dump = nullptr;
    const char* debug_generic_surface_snapshot_prefix = nullptr;
    const char* debug_nv2a_surface_snapshot_prefix = nullptr;
    const char* frame_series_prefix = nullptr;
    const char* test_input_capture_prefix = nullptr;
    const char* test_input_script = nullptr;
    const char* net_peer_host = nullptr;
    uint64_t frame_series_every = 0;
    uint64_t net_bind_port = 0;
    uint64_t net_peer_port = 0;
    uint64_t max_host_memory_mb = 0;
    uint32_t debug_surface_address = 0;
    uint32_t debug_surface_pitch = 0;
    uint32_t debug_surface_width = 0;
    uint32_t debug_surface_height = 0;
    uint32_t debug_generic_surface_snapshot_address = 0;
    uint64_t debug_generic_surface_snapshot_skip = 0;
    uint64_t debug_generic_surface_snapshot_count = 0;
    uint64_t debug_generic_surface_snapshot_interval = 1;
    uint32_t debug_nv2a_surface_snapshot_address = 0;
    uint64_t debug_nv2a_surface_snapshot_skip = 0;
    uint64_t debug_nv2a_surface_snapshot_count = 0;
    uint64_t debug_nv2a_surface_snapshot_interval = 1;
    uint64_t max_insns = 128ull * 1000 * 1000;  // 2BL kernel LZX decompress needs tens of M
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--serve")) serve = true;
        else if (!std::strcmp(argv[i], "--window")) window = true;
        else if (!std::strcmp(argv[i], "--interactive")) interactive = true;
        else if (!std::strcmp(argv[i], "--test-synthetic-controller-hub"))
            test_synthetic_controller_hub = true;
        else if (!std::strcmp(argv[i], "--test-synthetic-controller-hub-input-down"))
            test_synthetic_controller_hub_input_down = true;
        else if (!std::strcmp(argv[i], "--test-synthetic-controller-hub-input-down-a"))
            test_synthetic_controller_hub_input_down_a = true;
        else if (!std::strcmp(argv[i], "--reduced-retire-diag"))
            reduced_retire_diag = true;
        else if (!std::strcmp(argv[i], "--perf-accounting"))
            perf_accounting = true;
        else if (!std::strcmp(argv[i], "--net-disconnected"))
            net_disconnected = true;
        else if (!std::strcmp(argv[i], "--nv2a-renderer")) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "[nv2a-renderer] use --nv2a-renderer software|opengl\n");
                return 1;
            }
            nv2a_renderer_name = argv[++i];
            if (std::strcmp(nv2a_renderer_name, "software") &&
                std::strcmp(nv2a_renderer_name, "opengl")) {
                std::fprintf(stderr,
                             "[nv2a-renderer] invalid renderer '%s'; use software or opengl\n",
                             nv2a_renderer_name);
                return 1;
            }
        }
        else if (!std::strcmp(argv[i], "--pfifo-clear-trace")) {
            if (i + 1 >= argc) {
                std::fprintf(stderr,
                             "[host] use --pfifo-clear-trace <32-bit-color-base>\n");
                return 1;
            }
            const char* address = argv[++i];
            char* end = nullptr;
            errno = 0;
            const unsigned long long value = std::strtoull(address, &end, 0);
            if (!address[0] || address[0] == '-' || address[0] == '+' ||
                std::isspace(static_cast<unsigned char>(address[0])) || errno == ERANGE ||
                !end || *end != '\0' || value > 0xFFFFFFFFull) {
                std::fprintf(stderr,
                             "[host] --pfifo-clear-trace needs a 32-bit address (for example 0x03c8c000)\n");
                return 1;
            }
            pfifo_clear_trace = true;
            pfifo_clear_trace_target = static_cast<uint32_t>(value);
        }
        else if (!std::strcmp(argv[i], "--boot") && i + 1 < argc) boot_flash = argv[++i];
        else if (!std::strcmp(argv[i], "--run2bl") && i + 1 < argc) run2bl = argv[++i];
        else if (!std::strcmp(argv[i], "--hdd") && i + 1 < argc) hdd = argv[++i];
        else if (!std::strcmp(argv[i], "--eeprom") && i + 1 < argc) eeprom = argv[++i];
        else if (!std::strcmp(argv[i], "--frame-dump") && i + 1 < argc)
            frame_dump = argv[++i];
        else if (!std::strcmp(argv[i], "--debug-surface-dump")) {
            if (i + 5 >= argc) {
                std::fprintf(
                    stderr,
                    "[host] use --debug-surface-dump <path> <addr> <pitch> <width> <height>\n");
                return 1;
            }
            debug_surface_dump = argv[++i];
            if (!debug_surface_dump[0] ||
                !parse_u32_arg(argv[++i], debug_surface_address) ||
                !parse_u32_arg(argv[++i], debug_surface_pitch) ||
                !parse_u32_arg(argv[++i], debug_surface_width) ||
                !parse_u32_arg(argv[++i], debug_surface_height)) {
                std::fprintf(
                    stderr,
                    "[host] --debug-surface-dump expects path plus 32-bit addr/pitch/width/height\n");
                return 1;
            }
        }
        else if (!std::strcmp(argv[i], "--debug-generic-surface-snapshots")) {
            if (i + 5 >= argc) {
                std::fprintf(
                    stderr,
                    "[host] use --debug-generic-surface-snapshots <path-prefix> <addr> <skip> <count> <interval>\n");
                return 1;
            }
            debug_generic_surface_snapshot_prefix = argv[++i];
            if (!debug_generic_surface_snapshot_prefix[0] ||
                !parse_u32_arg(argv[++i], debug_generic_surface_snapshot_address) ||
                !parse_u64_arg(argv[++i], debug_generic_surface_snapshot_skip) ||
                !parse_u64_arg(argv[++i], debug_generic_surface_snapshot_count) ||
                !parse_u64_arg(argv[++i], debug_generic_surface_snapshot_interval) ||
                debug_generic_surface_snapshot_count == 0 ||
                debug_generic_surface_snapshot_interval == 0) {
                std::fprintf(
                    stderr,
                    "[host] --debug-generic-surface-snapshots expects prefix, 32-bit addr, and positive count/interval\n");
                return 1;
            }
        }
        else if (!std::strcmp(argv[i], "--debug-nv2a-surface-snapshots")) {
            if (i + 5 >= argc) {
                std::fprintf(
                    stderr,
                    "[host] use --debug-nv2a-surface-snapshots <path-prefix> <addr> <skip> <count> <interval>\n");
                return 1;
            }
            debug_nv2a_surface_snapshot_prefix = argv[++i];
            if (!debug_nv2a_surface_snapshot_prefix[0] ||
                !parse_u32_arg(argv[++i], debug_nv2a_surface_snapshot_address) ||
                !parse_u64_arg(argv[++i], debug_nv2a_surface_snapshot_skip) ||
                !parse_u64_arg(argv[++i], debug_nv2a_surface_snapshot_count) ||
                !parse_u64_arg(argv[++i], debug_nv2a_surface_snapshot_interval) ||
                debug_nv2a_surface_snapshot_count == 0 ||
                debug_nv2a_surface_snapshot_interval == 0) {
                std::fprintf(
                    stderr,
                    "[host] --debug-nv2a-surface-snapshots expects prefix, 32-bit addr, and positive count/interval\n");
                return 1;
            }
        }
        else if (!std::strcmp(argv[i], "--test-input-capture-prefix") && i + 1 < argc)
            test_input_capture_prefix = argv[++i];
        else if (!std::strcmp(argv[i], "--test-input-script") && i + 1 < argc)
            test_input_script = argv[++i];
        else if (!std::strcmp(argv[i], "--frame-series")) {
            if (i + 2 >= argc) {
                std::fprintf(
                    stderr,
                    "[host] use --frame-series <path-prefix> <instruction-interval>\n");
                return 1;
            }
            frame_series_prefix = argv[++i];
            const char* interval = argv[++i];
            char* end = nullptr;
            errno = 0;
            frame_series_every = std::strtoull(interval, &end, 0);
            if (!frame_series_prefix[0] || interval[0] == '-' ||
                errno == ERANGE || !end || *end != '\0' ||
                frame_series_every < kMinFrameSeriesInterval) {
                std::fprintf(
                    stderr,
                    "[host] frame-series interval must be an integer >= %llu "
                    "(sampled at guest 1 kHz)\n",
                    static_cast<unsigned long long>(kMinFrameSeriesInterval));
                return 1;
            }
        }
        else if (!std::strcmp(argv[i], "--net-udp-bind") && i + 1 < argc)
            net_bind_port = std::strtoull(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--net-udp-peer") && i + 2 < argc) {
            net_peer_host = argv[++i];
            net_peer_port = std::strtoull(argv[++i], nullptr, 0);
        }
        else if (!std::strcmp(argv[i], "--max-insns") && i + 1 < argc)
            max_insns = std::strtoull(argv[++i], nullptr, 0);
        else if (!std::strcmp(argv[i], "--max-host-memory-mb") &&
                 i + 1 < argc) {
            const char* limit = argv[++i];
            char* end = nullptr;
            errno = 0;
            max_host_memory_mb = std::strtoull(limit, &end, 0);
            if (!limit[0] || limit[0] == '-' || errno == ERANGE || !end ||
                *end != '\0' || max_host_memory_mb == 0 ||
                max_host_memory_mb > UINT64_MAX / (1024ull * 1024ull)) {
                std::fprintf(
                    stderr,
                    "[host] --max-host-memory-mb needs a positive integer\n");
                return 1;
            }
        }
        else {
            std::fprintf(stderr, "[host] unknown or incomplete option: %s\n", argv[i]);
            return 1;
        }
    }

    const bool net_requested =
        net_bind_port != 0 || net_peer_host != nullptr || net_peer_port != 0;
    const bool frame_series_requested = frame_series_prefix != nullptr;
    if (net_requested &&
        (net_bind_port == 0 || net_bind_port > 65535 || !net_peer_host ||
         net_peer_port == 0 || net_peer_port > 65535)) {
        std::fprintf(stderr,
                     "[net] use --net-udp-bind <1..65535> "
                     "--net-udp-peer <host> <1..65535>\n");
        return 1;
    }
    if (net_disconnected && net_requested) {
        std::fprintf(
            stderr,
            "[net] --net-disconnected cannot be combined with a UDP Ethernet peer\n");
        return 1;
    }
    if (test_synthetic_controller_hub && (!boot_flash || !run2bl)) {
        std::fprintf(stderr,
                     "[input] --test-synthetic-controller-hub requires both "
                     "--boot <flash.bin> and --run2bl <2bl.bin>\n");
        return 1;
    }
    if (test_input_capture_prefix && !test_synthetic_controller_hub_input_down &&
        !test_synthetic_controller_hub_input_down_a && !test_input_script) {
        std::fprintf(stderr,
                     "[input] --test-input-capture-prefix is valid only with "
                     "a test-only synthetic Controller S input experiment\n");
        return 1;
    }
    if (test_input_script &&
        (test_synthetic_controller_hub_input_down ||
         test_synthetic_controller_hub_input_down_a)) {
        std::fprintf(stderr,
                     "[input] --test-input-script cannot be combined with "
                     "--test-synthetic-controller-hub-input-down(-a); choose one "
                     "test-only synthetic Controller S input experiment\n");
        return 1;
    }
    if (test_input_script &&
        (!test_synthetic_controller_hub || !boot_flash || !run2bl ||
         !test_input_capture_prefix || !test_input_capture_prefix[0])) {
        std::fprintf(stderr,
                     "[input] --test-input-script requires "
                     "--test-synthetic-controller-hub, --boot <flash.bin>, and "
                     "--run2bl <2bl.bin>, plus --test-input-capture-prefix <path>\n");
        return 1;
    }
    if (test_input_script && interactive) {
        std::fprintf(stderr,
                     "[input] --test-input-script cannot be combined with "
                     "--interactive\n");
        return 1;
    }
    if (test_input_script && net_requested) {
        std::fprintf(stderr,
                     "[input] --test-input-script cannot be combined with "
                     "live UDP Ethernet\n");
        return 1;
    }
    if (test_synthetic_controller_hub_input_down &&
        test_synthetic_controller_hub_input_down_a) {
        std::fprintf(stderr,
                     "[input] choose only one test-only synthetic Controller S input "
                     "experiment\n");
        return 1;
    }
    if (test_synthetic_controller_hub_input_down &&
        (!test_synthetic_controller_hub || !boot_flash || !run2bl ||
         !test_input_capture_prefix || !test_input_capture_prefix[0])) {
        std::fprintf(stderr,
                     "[input] --test-synthetic-controller-hub-input-down requires "
                     "--test-synthetic-controller-hub, --boot <flash.bin>, and "
                     "--run2bl <2bl.bin>, plus --test-input-capture-prefix <path>\n");
        return 1;
    }
    if (test_synthetic_controller_hub_input_down && interactive) {
        std::fprintf(stderr,
                     "[input] --test-synthetic-controller-hub-input-down cannot be "
                     "combined with --interactive\n");
        return 1;
    }
    if (test_synthetic_controller_hub_input_down && net_requested) {
        std::fprintf(stderr,
                     "[input] --test-synthetic-controller-hub-input-down cannot be "
                     "combined with live UDP Ethernet\n");
        return 1;
    }
    if (test_synthetic_controller_hub_input_down_a &&
        (!test_synthetic_controller_hub || !boot_flash || !run2bl ||
         !test_input_capture_prefix || !test_input_capture_prefix[0])) {
        std::fprintf(stderr,
                     "[input] --test-synthetic-controller-hub-input-down-a requires "
                     "--test-synthetic-controller-hub, --boot <flash.bin>, and "
                     "--run2bl <2bl.bin>, plus --test-input-capture-prefix <path>\n");
        return 1;
    }
    if (test_synthetic_controller_hub_input_down_a && interactive) {
        std::fprintf(stderr,
                     "[input] --test-synthetic-controller-hub-input-down-a cannot be "
                     "combined with --interactive\n");
        return 1;
    }
    if (test_synthetic_controller_hub_input_down_a && net_requested) {
        std::fprintf(stderr,
                     "[input] --test-synthetic-controller-hub-input-down-a cannot be "
                     "combined with live UDP Ethernet\n");
        return 1;
    }

    std::unique_ptr<Nv2aRenderer> nv2a_renderer;
    if (!std::strcmp(nv2a_renderer_name, "opengl")) {
        std::string renderer_error;
        nv2a_renderer = create_nv2a_opengl_renderer(renderer_error);
        if (!nv2a_renderer) {
            std::fprintf(stderr, "[nv2a-renderer] %s\n", renderer_error.c_str());
            return 1;
        }
    }
    std::printf("[nv2a-renderer] selected=%s generic-combiner=%s\n",
                nv2a_renderer_name,
                nv2a_renderer ? "accelerated when supported, otherwise software" :
                                "software");

    const uint64_t max_host_memory_bytes =
        max_host_memory_mb * 1024ull * 1024ull;
    if (max_host_memory_bytes != 0) {
        std::string memory_limit_error;
        if (!install_host_memory_limit(max_host_memory_bytes,
                                       memory_limit_error)) {
            std::fprintf(stderr, "[host] memory limit unavailable: %s\n",
                         memory_limit_error.c_str());
            return 1;
        }
        std::printf(
            "[host] hard process-memory limit armed: %llu MiB; "
            "soft stop at 90%%\n",
            static_cast<unsigned long long>(max_host_memory_mb));
    }

    // --boot <flash.bin>: stand up the machine (RAM + flash) and smoke-test the map.
    if (boot_flash) {
        XboxMachine::Config machine_config;
        machine_config.nv2a_renderer = nv2a_renderer.get();
        if (test_synthetic_controller_hub) {
            machine_config.ohci0_topology =
                Ohci::UsbTopology::kSyntheticControllerSHubForTest;
        }
        XboxMachine mach(machine_config);
        if (!mach.ok()) { std::fprintf(stderr, "[machine] unicorn init failed\n"); return 1; }
        if (!mach.init_ram()) { std::fprintf(stderr, "[machine] RAM map failed\n"); return 1; }
        if (!mach.load_flash(boot_flash)) {
            std::fprintf(stderr, "[machine] flash load failed: %s\n", boot_flash);
            return 1;
        }
        std::printf("[machine] 64MB RAM @0x0 + flash (%zu bytes, mirrored to 0xFFFFFFFF) mapped\n",
                    mach.flash_size());
        uint8_t hdr[8] = {0}, rst[4] = {0};
        mach.read(0xFF000000, hdr, 8);
        mach.read(0xFFFFFFF0, rst, 4);
        std::printf("[machine] flash @0xFF000000: %02x%02x%02x%02x%02x%02x%02x%02x "
                    "(NV2A-ptr header)\n", hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5],
                    hdr[6], hdr[7]);
        std::printf("[machine] reset @0xFFFFFFF0: %02x %02x %02x %02x (expect eb c6 = jmp short)\n",
                    rst[0], rst[1], rst[2], rst[3]);
        if (run2bl) {
            // Load the decrypted 2BL at its RAM base (0x90000, entry 0x901ee) and run it.
            // MCPX handoff state approximated (protected mode, flat segs, a scratch stack);
            // the prologue is CR0/CR3/MTRR setup with no stack use, so this suffices to start.
            if (!mach.load_blob(0x90000, run2bl)) {
                std::fprintf(stderr, "[2bl] load failed: %s\n", run2bl);
                return 1;
            }
            // Stand up the I/O fabric (PCI host, SMBus+SMC, NV2A) the 2BL platform-init drives.
            mach.init_devices();
            if (test_synthetic_controller_hub) {
                std::printf("[input] TEST-ONLY synthetic Controller S hub topology enabled; "
                            "not retail evidence\n");
            }
            XidDownReportScript down_script;
            XidDownThenAReportScript down_a_script;
            GenericInputScript generic_script;
            if (test_input_script) {
                std::ifstream script_file(test_input_script);
                if (!script_file.is_open()) {
                    std::fprintf(stderr, "[input] --test-input-script: cannot open %s\n",
                                 test_input_script);
                    return 1;
                }
                std::vector<GenericInputScript::Event> events;
                std::string script_error;
                if (!GenericInputScript::parse(script_file, events, script_error)) {
                    std::fprintf(stderr, "[input] --test-input-script %s: %s\n",
                                 test_input_script, script_error.c_str());
                    return 1;
                }
                std::printf("[input] TEST-ONLY generic input script armed: %s events=%zu "
                            "capture-prefix=%s; guest polls required\n",
                            test_input_script, events.size(), test_input_capture_prefix);
                generic_script.reset(std::move(events));
            }
            if (test_synthetic_controller_hub_input_down) {
                std::printf("[input] TEST-ONLY deterministic XID Down script armed: "
                            "endpoint-2 threshold=%llu down=%llu neutral=%llu "
                            "capture-prefix=%s; guest polls required\n",
                            static_cast<unsigned long long>(
                                XidDownReportScript::kMenuReadyEndpoint2Reports),
                            static_cast<unsigned long long>(
                                XidDownReportScript::kDownDeliveredReports),
                            static_cast<unsigned long long>(
                                XidDownReportScript::kNeutralDeliveredReports),
                            test_input_capture_prefix);
            }
            if (test_synthetic_controller_hub_input_down_a) {
                std::printf(
                    "[input] TEST-ONLY deterministic XID Down-then-A script armed: "
                    "endpoint-2 threshold=%llu down=%llu neutral=%llu pre-a=%llu "
                    "a=%llu neutral-after-a=%llu post-a-settle=%llu final-neutral=%llu "
                    "capture-prefix=%s; guest polls required\n",
                    static_cast<unsigned long long>(
                        XidDownThenAReportScript::kMenuReadyEndpoint2Reports),
                    static_cast<unsigned long long>(
                        XidDownThenAReportScript::kDownDeliveredReports),
                    static_cast<unsigned long long>(
                        XidDownThenAReportScript::kNeutralAfterDownReports),
                    static_cast<unsigned long long>(
                        XidDownThenAReportScript::kPreAEndpoint2Reports),
                    static_cast<unsigned long long>(
                        XidDownThenAReportScript::kADeliveredReports),
                    static_cast<unsigned long long>(
                        XidDownThenAReportScript::kNeutralAfterAReports),
                    static_cast<unsigned long long>(
                        XidDownThenAReportScript::kPostACaptureSettlingReports),
                    static_cast<unsigned long long>(
                        XidDownThenAReportScript::kFinalNeutralReports),
                    test_input_capture_prefix);
            }
            if (pfifo_clear_trace && mach.nv2a()) {
                mach.nv2a()->arm_color_clear_trace(pfifo_clear_trace_target);
                std::printf("[host] PFIFO clear trace armed for color base 0x%08x\n",
                            pfifo_clear_trace_target);
            }
            if (debug_generic_surface_snapshot_prefix && mach.nv2a()) {
                uint64_t matched_batches = 0;
                uint64_t written_snapshots = 0;
                bool snapshot_failure_logged = false;
                mach.nv2a()->on_generic_batch_complete(
                    [&, matched_batches, written_snapshots,
                     snapshot_failure_logged](const Nv2a::GenericBatchDebugInfo& info)
                        mutable {
                            if (info.color_address !=
                                debug_generic_surface_snapshot_address)
                                return;
                            ++matched_batches;
                            const bool after_skip =
                                matched_batches > debug_generic_surface_snapshot_skip;
                            const bool on_interval =
                                after_skip &&
                                ((matched_batches -
                                  debug_generic_surface_snapshot_skip - 1) %
                                     debug_generic_surface_snapshot_interval ==
                                 0);
                            if (!on_interval ||
                                written_snapshots >=
                                    debug_generic_surface_snapshot_count) {
                                return;
                            }
                            std::vector<uint8_t> surface_rgba;
                            std::string surface_error;
                            const bool captured = capture_argb32_surface(
                                mach, info.color_address, info.pitch, info.width,
                                info.height, surface_rgba, surface_error);
                            char path[1024];
                            const int path_len = std::snprintf(
                                path, sizeof(path),
                                "%s-match%06llu-batch%010llu.ppm",
                                debug_generic_surface_snapshot_prefix,
                                static_cast<unsigned long long>(matched_batches),
                                static_cast<unsigned long long>(info.batch));
                            const bool path_ok =
                                path_len > 0 &&
                                static_cast<size_t>(path_len) < sizeof(path);
                            const bool written =
                                captured && path_ok &&
                                write_frame_ppm(path, surface_rgba, info.width,
                                                info.height);
                            if (written) {
                                ++written_snapshots;
                                std::printf(
                                    "[debug-generic-surface] wrote %s target=%08x "
                                    "match=%llu batch=%llu fp=%016llx full=%016llx "
                                    "prim=%u verts=%u sfmt=%08x shader=%08x "
                                    "comb=%08x frags=%llu writes=%llu texmod=%u "
                                    "exact=%u reject=%u hash=%016llx\n",
                                    path, info.color_address,
                                    static_cast<unsigned long long>(matched_batches),
                                    static_cast<unsigned long long>(info.batch),
                                    static_cast<unsigned long long>(info.fingerprint),
                                    static_cast<unsigned long long>(info.full_state_hash),
                                    info.primitive_mode, info.vertex_count,
                                    info.surface_format,
                                    info.shader_stage_program,
                                    info.combiner_control,
                                    static_cast<unsigned long long>(info.fragments),
                                    static_cast<unsigned long long>(info.writes),
                                    info.texture_modulate_observed ? 1u : 0u,
                                    info.texture_modulate_exact ? 1u : 0u,
                                    info.texture_modulate_reject,
                                    static_cast<unsigned long long>(
                                        frame_hash(surface_rgba)));
                            } else if (!snapshot_failure_logged) {
                                snapshot_failure_logged = true;
                                std::printf(
                                    "[debug-generic-surface] FAILED target=%08x "
                                    "match=%llu batch=%llu pitch=%u size=%ux%u%s%s%s\n",
                                    info.color_address,
                                    static_cast<unsigned long long>(matched_batches),
                                    static_cast<unsigned long long>(info.batch),
                                    info.pitch, info.width, info.height,
                                    surface_error.empty() ? "" : " (",
                                    surface_error.empty()
                                        ? ""
                                        : surface_error.c_str(),
                                    surface_error.empty() ? "" : ")");
                            }
                        });
                std::printf(
                    "[host] generic surface snapshots armed: prefix=%s target=%08x "
                    "skip=%llu count=%llu interval=%llu\n",
                    debug_generic_surface_snapshot_prefix,
                    debug_generic_surface_snapshot_address,
                    static_cast<unsigned long long>(
                        debug_generic_surface_snapshot_skip),
                    static_cast<unsigned long long>(
                        debug_generic_surface_snapshot_count),
                    static_cast<unsigned long long>(
                        debug_generic_surface_snapshot_interval));
            }
            if (debug_nv2a_surface_snapshot_prefix && mach.nv2a()) {
                uint64_t matched_events = 0;
                uint64_t written_snapshots = 0;
                bool snapshot_failure_logged = false;
                mach.nv2a()->on_surface_write_complete(
                    [&, matched_events, written_snapshots,
                     snapshot_failure_logged](const Nv2a::SurfaceWriteDebugInfo& info)
                        mutable {
                            if (info.color_address !=
                                debug_nv2a_surface_snapshot_address)
                                return;
                            ++matched_events;
                            const bool after_skip =
                                matched_events > debug_nv2a_surface_snapshot_skip;
                            const bool on_interval =
                                after_skip &&
                                ((matched_events -
                                  debug_nv2a_surface_snapshot_skip - 1) %
                                     debug_nv2a_surface_snapshot_interval ==
                                 0);
                            if (!on_interval ||
                                written_snapshots >=
                                    debug_nv2a_surface_snapshot_count) {
                                return;
                            }
                            std::vector<uint8_t> surface_rgba;
                            std::string surface_error;
                            const bool captured = capture_argb32_surface(
                                mach, info.color_address, info.pitch, info.width,
                                info.height, surface_rgba, surface_error);
                            char path[1024];
                            const int path_len = std::snprintf(
                                path, sizeof(path),
                                "%s-event%06llu-%s-seq%010llu.ppm",
                                debug_nv2a_surface_snapshot_prefix,
                                static_cast<unsigned long long>(matched_events),
                                info.kind,
                                static_cast<unsigned long long>(info.sequence));
                            const bool path_ok =
                                path_len > 0 &&
                                static_cast<size_t>(path_len) < sizeof(path);
                            const bool written =
                                captured && path_ok &&
                                write_frame_ppm(path, surface_rgba, info.width,
                                                info.height);
                            if (written) {
                                ++written_snapshots;
                                std::printf(
                                    "[debug-nv2a-surface] wrote %s kind=%s "
                                    "target=%08x event=%llu seq=%llu profile=%u "
                                    "fp=%016llx full=%016llx prim=%u verts=%u "
                                    "sfmt=%08x shader=%08x comb=%08x "
                                    "frags=%llu writes=%llu hash=%016llx\n",
                                    path, info.kind, info.color_address,
                                    static_cast<unsigned long long>(matched_events),
                                    static_cast<unsigned long long>(info.sequence),
                                    info.profile,
                                    static_cast<unsigned long long>(info.fingerprint),
                                    static_cast<unsigned long long>(info.full_state_hash),
                                    info.primitive_mode, info.vertex_count,
                                    info.surface_format,
                                    info.shader_stage_program,
                                    info.combiner_control,
                                    static_cast<unsigned long long>(info.fragments),
                                    static_cast<unsigned long long>(info.writes),
                                    static_cast<unsigned long long>(
                                        frame_hash(surface_rgba)));
                            } else if (!snapshot_failure_logged) {
                                snapshot_failure_logged = true;
                                std::printf(
                                    "[debug-nv2a-surface] FAILED kind=%s "
                                    "target=%08x event=%llu seq=%llu pitch=%u "
                                    "size=%ux%u%s%s%s\n",
                                    info.kind, info.color_address,
                                    static_cast<unsigned long long>(matched_events),
                                    static_cast<unsigned long long>(info.sequence),
                                    info.pitch, info.width, info.height,
                                    surface_error.empty() ? "" : " (",
                                    surface_error.empty()
                                        ? ""
                                        : surface_error.c_str(),
                                    surface_error.empty() ? "" : ")");
                            }
                        });
                std::printf(
                    "[host] NV2A surface snapshots armed: prefix=%s target=%08x "
                    "skip=%llu count=%llu interval=%llu\n",
                    debug_nv2a_surface_snapshot_prefix,
                    debug_nv2a_surface_snapshot_address,
                    static_cast<unsigned long long>(
                        debug_nv2a_surface_snapshot_skip),
                    static_cast<unsigned long long>(
                        debug_nv2a_surface_snapshot_count),
                    static_cast<unsigned long long>(
                        debug_nv2a_surface_snapshot_interval));
            }
            if (hdd) {
                if (!mach.attach_hdd(hdd)) {
                    std::fprintf(stderr, "[hdd] attach failed: %s\n", hdd);
                    return 1;
                }
                std::printf("[hdd] attached %s (IDE drive 0, security LOCKED)\n", hdd);
            } else {
                std::printf("[hdd] no --hdd image: IDE drive 0 reports absent\n");
            }
            if (eeprom) {
                if (!mach.attach_eeprom(eeprom)) {
                    std::fprintf(stderr, "[eeprom] attach failed (need 256-byte dump): %s\n",
                                 eeprom);
                    return 1;
                }
                std::printf("[eeprom] attached %s (SMBus 0x54)\n", eeprom);
            } else {
                std::printf("[eeprom] no --eeprom image: SMBus 0x54 reports absent\n");
            }
            // The existing frame viewer opens after execution, when it cannot feed the guest.
            // Keep this opt-in SDL input window alive for the whole emulation run instead.
            SdlKeyboardInput host_input;
            if (interactive) {
                if (!host_input.open("xboxlle input — arrows=D-pad, Z=A, X=B, Enter=Start, Backspace=Back"))
                    return 1;
                std::printf("[input] interactive Controller S: arrows=D-pad Z=A X=B "
                            "Enter=Start Backspace=Back\n");
            }

            std::unique_ptr<EthernetUdpTransport> net_transport;
            std::array<uint8_t, 65536> net_frame{};
            // Reused by the throttled live presenter; avoid a full frame copy at every poll.
            std::vector<uint8_t> live_rgba;
            Nv2a::ScanoutInfo live_scanout;
            std::string live_scanout_error;
            uint64_t next_frame_series_insn = frame_series_every;
            bool frame_series_exhausted = false;
            auto frame_series_interval_start = std::chrono::steady_clock::now();
            uint64_t frame_series_vblanks_start =
                mach.nv2a() ? mach.nv2a()->vblanks() : 0;
            auto print_memory_diagnostics = [&]() {
                if (!perf_accounting) return;
                HostMemoryUsage process;
                HostHeapUsage heaps;
                const bool process_ok = query_host_memory(process);
                const bool heaps_ok = query_host_heaps(heaps);
                if (process_ok) {
                    std::printf(
                        "[host-memory] working-set=%llu private-commit=%llu",
                        static_cast<unsigned long long>(process.working_set),
                        static_cast<unsigned long long>(process.private_commit));
                    if (heaps_ok) {
                        std::printf(
                            " heap-count=%llu heap-allocated=%llu "
                            "heap-committed=%llu heap-reserved=%llu",
                            static_cast<unsigned long long>(heaps.heaps),
                            static_cast<unsigned long long>(heaps.allocated),
                            static_cast<unsigned long long>(heaps.committed),
                            static_cast<unsigned long long>(heaps.reserved));
                    } else {
                        std::printf(" heap-summary=unavailable");
                    }
                    std::printf("\n");
                } else {
                    std::printf("[host-memory] process-counters=unavailable\n");
                }
                auto* unicorn = dynamic_cast<UnicornFloor*>(&mach.cpu());
                if (unicorn) {
                    uint64_t code_regions = 0;
                    uint64_t count_regions = 0;
                    unicorn->hook_region_counts(code_regions, count_regions);
                    std::printf(
                        "[unicorn-memory] code-hook-regions=%llu "
                        "count-hook-regions=%llu\n",
                        static_cast<unsigned long long>(code_regions),
                        static_cast<unsigned long long>(count_regions));
                }
            };
            auto print_generic_combiner_perf_report = [&]() {
                if (!xlle::perf::g_enabled || !mach.nv2a()) return;
                std::vector<xlle::Nv2a::GenericCombinerFingerprintStats> fingerprints;
                mach.nv2a()->generic_combiner_fingerprint_perf_census(fingerprints);
                std::sort(fingerprints.begin(), fingerprints.end(),
                          [](const auto& a, const auto& b) {
                              if (a.wall_ns != b.wall_ns) return a.wall_ns > b.wall_ns;
                              if (a.fragments != b.fragments)
                                  return a.fragments > b.fragments;
                              return a.batches > b.batches;
                          });
                std::printf("[perf-generic] fingerprints=%zu top=", fingerprints.size());
                Nv2a::RasterStateSample raster_exemplar;
                const size_t limit = std::min<size_t>(fingerprints.size(), 12);
                for (size_t i = 0; i < limit; ++i) {
                    const auto& entry = fingerprints[i];
                    std::printf(" %016llx:b%llu/f%llu/w%llu/ns%llu",
                                static_cast<unsigned long long>(entry.fingerprint),
                                static_cast<unsigned long long>(entry.batches),
                                static_cast<unsigned long long>(entry.fragments),
                                static_cast<unsigned long long>(entry.writes),
                                static_cast<unsigned long long>(entry.wall_ns));
                    if (entry.bbox_pixels || entry.max_batch_fragments ||
                        entry.max_batch_bbox_pixels) {
                        std::printf("/bb%llu/maxf%llu/maxbb%llu",
                                    static_cast<unsigned long long>(entry.bbox_pixels),
                                    static_cast<unsigned long long>(
                                        entry.max_batch_fragments),
                                    static_cast<unsigned long long>(
                                        entry.max_batch_bbox_pixels));
                    }
                    if (entry.raster_state_sample_index != UINT32_MAX &&
                        mach.nv2a()->raster_state_sample_at(
                            entry.raster_state_sample_index, raster_exemplar)) {
                        const auto& state = raster_exemplar.words;
                        std::printf(
                            ":prim%u/v%u-%u/en%08x/sfmt%08x/pitch%08x/sh%08x/comb%08x/surf%08x/full%016llx",
                            static_cast<unsigned>(entry.primitive_mode),
                            entry.min_vertices, entry.max_vertices,
                            state[Nv2a::kRasterEnables],
                            state[Nv2a::kRasterSurfaceFormat],
                            state[Nv2a::kRasterSurfacePitch],
                            state[Nv2a::kRasterShaderStageProgram],
                            state[Nv2a::kRasterCombinerControl],
                            state[Nv2a::kRasterSurfaceColorOffset],
                            static_cast<unsigned long long>(entry.full_state_hash));
                    }
                    if (entry.texture_modulate_batches) {
                        std::printf(":texmod%llu",
                                    static_cast<unsigned long long>(
                                        entry.texture_modulate_batches));
                        if (entry.texture_modulate_exact_batches) {
                            std::printf("/exact%llu",
                                        static_cast<unsigned long long>(
                                            entry.texture_modulate_exact_batches));
                        }
                        bool any_texmod_reject = false;
                        for (size_t reason = 0;
                             reason <
                             Nv2a::generic_texture_modulate_reject_reason_count();
                             ++reason) {
                            const uint64_t count =
                                entry.texture_modulate_reject_batches[reason];
                            if (!count) continue;
                            std::printf("%s%s:%llu",
                                        any_texmod_reject ? "," : "/reject=",
                                        Nv2a::generic_texture_modulate_reject_reason_name(
                                            reason),
                                        static_cast<unsigned long long>(count));
                            any_texmod_reject = true;
                        }
                    }
                    if (entry.resident_batches) {
                        std::printf(":resident%llu",
                                    static_cast<unsigned long long>(
                                        entry.resident_batches));
                        if (entry.resident_exact_batches) {
                            std::printf("/exact%llu",
                                        static_cast<unsigned long long>(
                                            entry.resident_exact_batches));
                        }
                        bool any_resident_reject = false;
                        for (size_t reason = 0;
                             reason < Nv2a::generic_resident_reject_reason_count();
                             ++reason) {
                            const uint64_t count =
                                entry.resident_reject_batches[reason];
                            if (!count) continue;
                            std::printf("%s%s:%llu",
                                        any_resident_reject ? "," : "/reject=",
                                        Nv2a::generic_resident_reject_reason_name(
                                            reason),
                                        static_cast<unsigned long long>(count));
                            any_resident_reject = true;
                        }
                    }
                }
                std::printf("\n");
                // Keep the compact top-12 line readable, but emit complete,
                // machine-parseable exemplars for the twelve hottest states so a
                // later backend can reproduce a dominant dashboard envelope
                // without another multi-billion-instruction replay.
                const size_t state_limit = std::min<size_t>(fingerprints.size(), 12);
                for (size_t i = 0; i < state_limit; ++i) {
                    const auto& entry = fingerprints[i];
                    if (entry.raster_state_sample_index == UINT32_MAX ||
                        !mach.nv2a()->raster_state_sample_at(
                            entry.raster_state_sample_index, raster_exemplar))
                        continue;
                    std::printf(
                        "[perf-generic-state] fp=%016llx full=%016llx words=%zu data=",
                        static_cast<unsigned long long>(entry.fingerprint),
                        static_cast<unsigned long long>(entry.full_state_hash),
                        raster_exemplar.words.size());
                    for (size_t word = 0; word < raster_exemplar.words.size(); ++word) {
                        if (word) std::printf(",");
                        std::printf("%08x", raster_exemplar.words[word]);
                    }
                    std::printf("\n");
                    if (entry.first_triangle_recorded) {
                        std::printf(
                            "[perf-generic-geom] fp=%016llx full=%016llx "
                            "xy16=%lld,%lld,%lld,%lld,%lld,%lld "
                            "bbox=%d,%d,%d,%d area16=%llu maxf=%llu maxbb=%llu\n",
                            static_cast<unsigned long long>(entry.fingerprint),
                            static_cast<unsigned long long>(entry.full_state_hash),
                            static_cast<long long>(entry.first_triangle_xy_16[0]),
                            static_cast<long long>(entry.first_triangle_xy_16[1]),
                            static_cast<long long>(entry.first_triangle_xy_16[2]),
                            static_cast<long long>(entry.first_triangle_xy_16[3]),
                            static_cast<long long>(entry.first_triangle_xy_16[4]),
                            static_cast<long long>(entry.first_triangle_xy_16[5]),
                            entry.first_triangle_bounds[0],
                            entry.first_triangle_bounds[1],
                            entry.first_triangle_bounds[2],
                            entry.first_triangle_bounds[3],
                            static_cast<unsigned long long>(
                                entry.first_triangle_area_16),
                            static_cast<unsigned long long>(
                                entry.max_batch_fragments),
                            static_cast<unsigned long long>(
                                entry.max_batch_bbox_pixels));
                    }
                    if (entry.first_vertices_recorded) {
                        std::printf(
                            "[perf-generic-verts] fp=%016llx full=%016llx count=%u",
                            static_cast<unsigned long long>(entry.fingerprint),
                            static_cast<unsigned long long>(entry.full_state_hash),
                            entry.first_vertex_count);
                        for (uint32_t vertex = 0; vertex < entry.first_vertex_count;
                             ++vertex) {
                            const auto& p = entry.first_vertex_position[vertex];
                            std::printf(" v%up=%.9g,%.9g,%.9g,%.9g",
                                        vertex, p[0], p[1], p[2], p[3]);
                            const auto& d = entry.first_vertex_diffuse[vertex];
                            std::printf(" d=%.9g,%.9g,%.9g,%.9g", d[0], d[1],
                                        d[2], d[3]);
                            const auto& s = entry.first_vertex_specular[vertex];
                            std::printf(" s=%.9g,%.9g,%.9g,%.9g", s[0], s[1],
                                        s[2], s[3]);
                            for (uint32_t stage = 0; stage < 4; ++stage) {
                                const auto& t =
                                    entry.first_vertex_texture[vertex][stage];
                                std::printf(" t%u=%.9g,%.9g,%.9g,%.9g",
                                            stage, t[0], t[1], t[2], t[3]);
                            }
                        }
                        std::printf("\n");
                    }
                }
            };
            auto print_resident_combiner_report = [&]() {
                if (!mach.nv2a()) return;
                const auto* nv2a = mach.nv2a();
                std::printf(
                    "[perf-resident] candidates=%llu supported=%llu "
                    "uploads=%llu reuses=%llu target-switch-resolves=%llu "
                    "submits=%llu rejects=",
                    static_cast<unsigned long long>(
                        nv2a->generic_combiner_resident_candidates()),
                    static_cast<unsigned long long>(
                        nv2a->generic_combiner_resident_supported()),
                    static_cast<unsigned long long>(
                        nv2a->generic_combiner_resident_uploads()),
                    static_cast<unsigned long long>(
                        nv2a->generic_combiner_resident_reuses()),
                    static_cast<unsigned long long>(
                        nv2a->generic_combiner_resident_target_switch_resolves()),
                    static_cast<unsigned long long>(
                        nv2a->generic_combiner_resident_submits()));
                bool any_reject = false;
                for (size_t i = 0;
                     i < Nv2a::generic_resident_reject_reason_count(); ++i) {
                    const uint64_t count =
                        nv2a->generic_combiner_resident_reject_batches(i);
                    if (!count) continue;
                    if (any_reject) std::printf(",");
                    std::printf("%s:%llu",
                                Nv2a::generic_resident_reject_reason_name(i),
                                static_cast<unsigned long long>(count));
                    any_reject = true;
                }
                if (!any_reject) std::printf("none");
                std::printf("\n");
                std::printf("[perf-texture-modulate] observed=%llu exact=%llu "
                            "rejects=",
                            static_cast<unsigned long long>(
                                nv2a->generic_combiner_texture_modulate_observed()),
                            static_cast<unsigned long long>(
                                nv2a->generic_combiner_texture_modulate_exact()));
                bool any_texture_reject = false;
                for (size_t i = 0;
                     i < Nv2a::generic_texture_modulate_reject_reason_count();
                     ++i) {
                    const uint64_t count =
                        nv2a->generic_combiner_texture_modulate_reject_batches(i);
                    if (!count) continue;
                    if (any_texture_reject) std::printf(",");
                    std::printf("%s:%llu",
                                Nv2a::generic_texture_modulate_reject_reason_name(i),
                                static_cast<unsigned long long>(count));
                    any_texture_reject = true;
                }
                if (!any_texture_reject) std::printf("none");
                std::printf("\n");
            };
            auto capture_frame_series = [&]() {
                if (!frame_series_requested || frame_series_exhausted ||
                    mach.total_insns() < next_frame_series_insn ||
                    !mach.nv2a())
                    return;

                const uint64_t deadline = next_frame_series_insn;
                const uint64_t sampled_at = mach.total_insns();
                const auto interval_end = std::chrono::steady_clock::now();
                const uint64_t sampled_vblanks = mach.nv2a()->vblanks();
                const uint64_t interval_ns = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        interval_end - frame_series_interval_start).count());
                const uint64_t interval_vblanks =
                    sampled_vblanks - frame_series_vblanks_start;
                const double effective_fps = interval_ns
                    ? static_cast<double>(interval_vblanks) * 1.0e9 /
                          static_cast<double>(interval_ns)
                    : 0.0;
                char path[1024];
                std::snprintf(
                    path, sizeof(path), "%s-%012llu.ppm",
                    frame_series_prefix,
                    static_cast<unsigned long long>(sampled_at));
                live_scanout_error.clear();
                const bool captured = mach.nv2a()->capture_scanout(
                    live_rgba, live_scanout, live_scanout_error);
                const bool written =
                    captured &&
                    write_frame_ppm(path, live_rgba,
                                    live_scanout.width, live_scanout.height);
                std::printf(
                    "[host] frame-series deadline=%llu insns=%llu "
                    "endpoint2=%llu vblank-delta=%llu wall-ns=%llu "
                    "effective-fps=%.3f hash=%016llx %s: %s%s%s\n",
                    static_cast<unsigned long long>(deadline),
                    static_cast<unsigned long long>(sampled_at),
                    static_cast<unsigned long long>(
                        mach.ohci0()
                            ? mach.ohci0()->xid_endpoint2_input_reports()
                            : 0),
                    static_cast<unsigned long long>(interval_vblanks),
                    static_cast<unsigned long long>(interval_ns),
                    effective_fps,
                    static_cast<unsigned long long>(
                        captured ? frame_hash(live_rgba) : 0),
                    path, written ? "written" : "FAILED",
                    live_scanout_error.empty() ? "" : " (",
                    live_scanout_error.empty()
                        ? ""
                        : (live_scanout_error + ")").c_str());
                const std::string perf_report = xlle::perf::format_report();
                if (!perf_report.empty()) std::printf("%s\n", perf_report.c_str());
                print_generic_combiner_perf_report();
                print_resident_combiner_report();
                const std::string memory_report =
                    xlle::perf::format_memory_report();
                if (!memory_report.empty())
                    std::printf("%s\n", memory_report.c_str());
                print_memory_diagnostics();
                // Long stock-dashboard probes redirect stdout to a file. Flush each
                // checkpoint so allocation growth is observable before a host-memory
                // safety stop instead of being lost in the stdio buffer.
                std::fflush(stdout);
                // Exclude diagnostic capture/write/sort time from the next interval;
                // --frame-series is an observer, not part of normal SDL throughput.
                frame_series_interval_start = std::chrono::steady_clock::now();
                frame_series_vblanks_start = sampled_vblanks;
                if (UINT64_MAX - next_frame_series_insn <
                    frame_series_every) {
                    frame_series_exhausted = true;
                } else {
                    next_frame_series_insn += frame_series_every;
                }
            };
            auto capture_test_input_frame = [&](const char* phase) {
                std::vector<uint8_t> rgba;
                Nv2a::ScanoutInfo scanout;
                std::string error;
                const uint64_t sampled_at = mach.total_insns();
                char path[1024];
                const int path_size = std::snprintf(
                    path, sizeof(path), "%s-%s-%012llu.ppm",
                    test_input_capture_prefix, phase,
                    static_cast<unsigned long long>(sampled_at));
                const bool path_ok = path_size > 0 &&
                                     static_cast<size_t>(path_size) < sizeof(path);
                const bool captured = path_ok && mach.nv2a() &&
                    mach.nv2a()->capture_scanout(rgba, scanout, error);
                const bool written = captured &&
                    write_frame_ppm(path, rgba, scanout.width, scanout.height);
                std::printf(
                    "[input] TEST-ONLY %s capture insns=%llu endpoint2=%llu aggregate=%llu "
                    "hash=%016llx %s: %s%s%s\n",
                    phase, static_cast<unsigned long long>(sampled_at),
                    static_cast<unsigned long long>(mach.ohci0()->xid_endpoint2_input_reports()),
                    static_cast<unsigned long long>(mach.ohci0()->xid_input_reports()),
                    static_cast<unsigned long long>(captured ? frame_hash(rgba) : 0),
                    path_ok ? path : "<path-too-long>", written ? "written" : "FAILED",
                    error.empty() ? "" : " (",
                    error.empty() ? "" : (error + ")").c_str());
                return written;
            };
            if (net_requested) {
                net_transport = std::make_unique<EthernetUdpTransport>();
                std::string net_error;
                if (!net_transport->bind(static_cast<uint16_t>(net_bind_port), net_error) ||
                    !net_transport->set_peer(
                        net_peer_host, static_cast<uint16_t>(net_peer_port), net_error)) {
                    std::fprintf(stderr, "[net] attach failed: %s\n", net_error.c_str());
                    return 1;
                }
                std::printf("[net] UDP Ethernet bind=0.0.0.0:%u peer=%s:%u\n",
                            static_cast<unsigned>(net_transport->local_port()),
                            net_peer_host, static_cast<unsigned>(net_peer_port));
                mach.nvnet()->set_packet_sink(
                    [&](const uint8_t* data, size_t size) {
                        std::string error;
                        if (!net_transport->send(data, size, error))
                            mach.cpu().request_stop("NVNet UDP transport: " + error);
                    });
            } else if (net_disconnected) {
                // The default link-up PHY remains the xemu boot witness. This opt-in
                // experiment changes the modeled link state before guest execution.
                // It is not yet evidence for exact cold-boot cable-out behavior.
                mach.nvnet()->set_link(false);
                std::printf("[net] Ethernet cable disconnected (opt-in)\n");
            }
            const uint64_t soft_host_memory_stop_bytes =
                max_host_memory_bytes - max_host_memory_bytes / 10;
            auto next_host_memory_check = std::chrono::steady_clock::now();
            if (interactive || net_requested || frame_series_requested ||
                max_host_memory_bytes != 0 ||
                test_synthetic_controller_hub_input_down ||
                test_synthetic_controller_hub_input_down_a || test_input_script) {
                mach.on_host_poll([&]() {
                    // Host event processing is nonblocking and guest state changes only on this
                    // emulation thread. XID endpoint 2 observes it on its next periodic TD.
                    if (max_host_memory_bytes != 0) {
                        const auto now = std::chrono::steady_clock::now();
                        if (now >= next_host_memory_check) {
                            next_host_memory_check = now + std::chrono::milliseconds(250);
                            HostMemoryUsage usage;
                            if (!query_host_memory(usage)) {
                                const char* reason =
                                    "GetProcessMemoryInfo failed while enforcing "
                                    "host memory limit";
                                std::fprintf(stderr, "[host] SAFETY STOP: %s\n", reason);
                                std::fflush(stderr);
                                mach.cpu().request_stop(reason);
                                return;
                            }
                            if (usage.working_set > soft_host_memory_stop_bytes ||
                                usage.private_commit > soft_host_memory_stop_bytes) {
                                char reason[192];
                                std::snprintf(
                                    reason, sizeof(reason),
                                    "host memory reached soft limit: working-set=%llu MiB "
                                    "private=%llu MiB hard-limit=%llu MiB",
                                    static_cast<unsigned long long>(
                                        usage.working_set / (1024ull * 1024ull)),
                                    static_cast<unsigned long long>(
                                        usage.private_commit / (1024ull * 1024ull)),
                                    static_cast<unsigned long long>(
                                        max_host_memory_mb));
                                std::fprintf(stderr, "[host] SAFETY STOP: %s\n", reason);
                                std::fflush(stderr);
                                mach.cpu().request_stop(reason);
                                return;
                            }
                        }
                    }
                    if (test_synthetic_controller_hub_input_down) {
                        const bool configured = mach.ohci0()->xid_configuration() == 1;
                        const uint64_t endpoint2_reports =
                            mach.ohci0()->xid_endpoint2_input_reports();
                        if (down_script.will_press(configured, endpoint2_reports) &&
                            !capture_test_input_frame("pre-down")) {
                            down_script.capture_failed();
                            std::printf(
                                "[input] TEST-ONLY NOT-DELIVERED capture-failed "
                                "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        const auto transition =
                            down_script.update(configured, endpoint2_reports);
                        if (transition.pressed) {
                            std::printf(
                                "[input] TEST-ONLY Down PRESS insns=%llu endpoint2=%llu "
                                "aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        if (transition.released) {
                            std::printf(
                                "[input] TEST-ONLY Down RELEASE insns=%llu endpoint2=%llu "
                                "aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        if (transition.delivery_failed) {
                            std::printf(
                                "[input] TEST-ONLY NOT-DELIVERED deconfigured "
                                "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        if (transition.completed) {
                            const bool captured = capture_test_input_frame("post-neutral");
                            if (!captured) {
                                down_script.capture_failed();
                                std::printf(
                                    "[input] TEST-ONLY NOT-DELIVERED post-capture-failed "
                                    "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                    static_cast<unsigned long long>(mach.total_insns()),
                                    static_cast<unsigned long long>(endpoint2_reports),
                                    static_cast<unsigned long long>(
                                        mach.ohci0()->xid_input_reports()));
                            } else {
                                std::printf(
                                    "[input] TEST-ONLY Down COMPLETE insns=%llu endpoint2=%llu "
                                    "aggregate=%llu post-capture=written\n",
                                    static_cast<unsigned long long>(mach.total_insns()),
                                    static_cast<unsigned long long>(endpoint2_reports),
                                    static_cast<unsigned long long>(
                                        mach.ohci0()->xid_input_reports()));
                            }
                        }
                        mach.ohci0()->set_xid_gamepad_state(
                            down_script.state());
                    } else if (test_synthetic_controller_hub_input_down_a) {
                        const bool configured = mach.ohci0()->xid_configuration() == 1;
                        const uint64_t endpoint2_reports =
                            mach.ohci0()->xid_endpoint2_input_reports();
                        if (down_a_script.will_press_down(configured, endpoint2_reports) &&
                            !capture_test_input_frame("pre-down-a")) {
                            down_a_script.capture_failed();
                            std::printf(
                                "[input] TEST-ONLY NOT-DELIVERED pre-capture-failed "
                                "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        if (down_a_script.will_press_a(configured, endpoint2_reports) &&
                            !capture_test_input_frame("pre-a-stable")) {
                            down_a_script.capture_failed();
                            std::printf(
                                "[input] TEST-ONLY NOT-DELIVERED pre-a-capture-failed "
                                "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        const auto transition =
                            down_a_script.update(configured, endpoint2_reports);
                        if (transition.down_pressed) {
                            std::printf(
                                "[input] TEST-ONLY Down PRESS insns=%llu endpoint2=%llu "
                                "aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        if (transition.down_released) {
                            std::printf(
                                "[input] TEST-ONLY Down RELEASE insns=%llu endpoint2=%llu "
                                "aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        if (transition.selected_capture_ready) {
                            if (!capture_test_input_frame("selected-my-files")) {
                                down_a_script.capture_failed();
                                std::printf(
                                    "[input] TEST-ONLY NOT-DELIVERED selected-capture-failed "
                                    "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                    static_cast<unsigned long long>(mach.total_insns()),
                                    static_cast<unsigned long long>(endpoint2_reports),
                                    static_cast<unsigned long long>(
                                        mach.ohci0()->xid_input_reports()));
                            } else {
                                std::printf(
                                    "[input] TEST-ONLY My Files SELECTED capture written "
                                    "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                    static_cast<unsigned long long>(mach.total_insns()),
                                    static_cast<unsigned long long>(endpoint2_reports),
                                    static_cast<unsigned long long>(
                                        mach.ohci0()->xid_input_reports()));
                            }
                        }
                        if (transition.a_pressed) {
                            std::printf(
                                "[input] TEST-ONLY A PRESS insns=%llu endpoint2=%llu "
                                "aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        if (transition.a_released) {
                            std::printf(
                                "[input] TEST-ONLY A RELEASE insns=%llu endpoint2=%llu "
                                "aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        if (transition.delivery_failed) {
                            std::printf(
                                "[input] TEST-ONLY NOT-DELIVERED deconfigured "
                                "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        if (transition.post_a_capture_ready) {
                            const bool captured =
                                capture_test_input_frame("post-a-neutral");
                            if (!captured) {
                                down_a_script.capture_failed();
                                std::printf(
                                    "[input] TEST-ONLY NOT-DELIVERED post-a-capture-failed "
                                    "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                    static_cast<unsigned long long>(mach.total_insns()),
                                    static_cast<unsigned long long>(endpoint2_reports),
                                    static_cast<unsigned long long>(
                                        mach.ohci0()->xid_input_reports()));
                            } else {
                                std::printf(
                                    "[input] TEST-ONLY post-A neutral capture written "
                                    "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                    static_cast<unsigned long long>(mach.total_insns()),
                                    static_cast<unsigned long long>(endpoint2_reports),
                                    static_cast<unsigned long long>(
                                        mach.ohci0()->xid_input_reports()));
                            }
                        }
                        if (transition.settled_capture_ready) {
                            const bool captured =
                                capture_test_input_frame("post-a-settled");
                            if (!captured) {
                                down_a_script.capture_failed();
                                std::printf(
                                    "[input] TEST-ONLY NOT-DELIVERED post-a-settled-capture-failed "
                                    "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                    static_cast<unsigned long long>(mach.total_insns()),
                                    static_cast<unsigned long long>(endpoint2_reports),
                                    static_cast<unsigned long long>(
                                        mach.ohci0()->xid_input_reports()));
                            } else {
                                std::printf(
                                    "[input] TEST-ONLY post-A settled capture written "
                                    "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                    static_cast<unsigned long long>(mach.total_insns()),
                                    static_cast<unsigned long long>(endpoint2_reports),
                                    static_cast<unsigned long long>(
                                        mach.ohci0()->xid_input_reports()));
                            }
                        }
                        if (transition.completed) {
                            const bool captured =
                                capture_test_input_frame("post-a-final-stable");
                            if (!captured) {
                                down_a_script.capture_failed();
                                std::printf(
                                    "[input] TEST-ONLY NOT-DELIVERED "
                                    "post-a-final-stable-capture-failed "
                                    "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                    static_cast<unsigned long long>(mach.total_insns()),
                                    static_cast<unsigned long long>(endpoint2_reports),
                                    static_cast<unsigned long long>(
                                        mach.ohci0()->xid_input_reports()));
                            } else {
                                std::printf(
                                    "[input] TEST-ONLY Down-then-A COMPLETE "
                                    "insns=%llu endpoint2=%llu aggregate=%llu "
                                    "post-a-final-stable-capture=written\n",
                                    static_cast<unsigned long long>(mach.total_insns()),
                                    static_cast<unsigned long long>(endpoint2_reports),
                                    static_cast<unsigned long long>(
                                        mach.ohci0()->xid_input_reports()));
                            }
                        }
                        mach.ohci0()->set_xid_gamepad_state(down_a_script.state());
                    } else if (test_input_script) {
                        // Generalized delivery/capture plumbing: identical endpoint-2-
                        // count-keyed model as the two branches above, but driven by
                        // parsed script events instead of hardcoded phases.
                        const uint64_t endpoint2_reports =
                            mach.ohci0()->xid_endpoint2_input_reports();
                        std::string capture_name;
                        if (generic_script.will_capture(endpoint2_reports, capture_name) &&
                            !capture_test_input_frame(capture_name.c_str())) {
                            std::printf(
                                "[input] TEST-ONLY NOT-DELIVERED %s-capture-failed "
                                "insns=%llu endpoint2=%llu aggregate=%llu\n",
                                capture_name.c_str(),
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        const auto transition = generic_script.update(endpoint2_reports);
                        if (transition.advanced) {
                            std::printf(
                                "[input] TEST-ONLY script event %zu insns=%llu "
                                "endpoint2=%llu aggregate=%llu buttons=%04x analog0=%02x\n",
                                generic_script.index(),
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()),
                                generic_script.state().buttons,
                                generic_script.state().analog[0]);
                        }
                        if (transition.completed) {
                            std::printf(
                                "[input] TEST-ONLY generic script COMPLETE insns=%llu "
                                "endpoint2=%llu aggregate=%llu\n",
                                static_cast<unsigned long long>(mach.total_insns()),
                                static_cast<unsigned long long>(endpoint2_reports),
                                static_cast<unsigned long long>(
                                    mach.ohci0()->xid_input_reports()));
                        }
                        mach.ohci0()->set_xid_gamepad_state(generic_script.state());
                    }
                    if (interactive) {
                        host_input.poll();
                        mach.ohci0()->set_xid_gamepad_state(host_input.state());
                        if (host_input.quit_requested())
                            mach.cpu().request_stop("host input window closed");
                        // capture_scanout copies the complete guest surface, so keep it on a
                        // host-wall-clock cadence rather than the 1 kHz guest poll cadence.
                        if (host_input.frame_due() && mach.nv2a()) {
                            if (mach.nv2a()->capture_scanout(
                                    live_rgba, live_scanout, live_scanout_error)) {
                                host_input.present_rgba(
                                    live_rgba.data(), static_cast<int>(live_scanout.width),
                                    static_cast<int>(live_scanout.height),
                                    static_cast<size_t>(live_scanout.width) * 4);
                            }
                        }
                    }
                    capture_frame_series();
                    if (!net_transport) return;
                    // Leave queued datagrams in the host socket until the guest has an
                    // enabled RX engine and an AVAIL descriptor, matching NIC backpressure.
                    for (unsigned packet = 0; packet < 32; ++packet) {
                        if (!mach.nvnet()->rx_ready()) break;
                        std::string error;
                        const int size =
                            net_transport->receive(net_frame.data(), net_frame.size(), error);
                        if (size < 0) {
                            mach.cpu().request_stop("NVNet UDP transport: " + error);
                            break;
                        }
                        if (size == 0) break;
                        const auto result =
                            mach.nvnet()->receive_packet(net_frame.data(),
                                                         static_cast<size_t>(size));
                        if (result == NvNet::RxResult::Fault ||
                            result == NvNet::RxResult::NotReady)
                            break;
                    }
                });
            }
            std::vector<IoEvent> io_log;
            uint64_t ide_io_count = 0;  // any access to 0x1F0-0x1F7 / 0x3F6 / bus-master ports
            mach.on_io_event([&](const IoEvent& e) {
                if ((e.port >= 0x1F0 && e.port <= 0x1F7) || e.port == 0x3F6 ||
                    (e.port >= 0xFF60 && e.port <= 0xFF6F))
                    ide_io_count++;
                io_log.push_back(e);
                if (io_log.size() > 64) io_log.erase(io_log.begin());
            });
            std::vector<MemEvent> mmio_log;
            mach.on_mmio_event([&](const MemEvent& e) {
                mmio_log.push_back(e);
                if (mmio_log.size() > 64) mmio_log.erase(mmio_log.begin());
            });
            // Keep the last 64 PCs in a fixed circular buffer (cheap on the per-insn hot path;
            // this fires for every one of the tens of millions of decompression instructions).
            uint32_t trace[64] = {0};
            uint64_t trace_n = 0;
            // Snapshot the PC history at each transition INTO the idle loop, so the final snapshot
            // shows the boot thread's last actions before it blocked and yielded to idle.
            uint32_t pre_idle[64] = {0};
            uint64_t pre_idle_n = 0;
            bool was_idle = false;
            auto in_idle = [](uint32_t pc) { return pc >= 0x8001e3ce && pc <= 0x8001e3ff; };
            bool idle_latched = false;
            // Thread attribution (kernel 4983 layout): KPCR at 0x8003da3c, PrcbData.CurrentThread
            // at KPCR+0x28, idle thread 0x8003d8f0. The pointer is polled every 64 insns (cheap);
            // every retired PC is recorded to the non-idle ring while a non-idle thread is
            // current, so we see exactly what phase-1 (or any worker) did last.
            constexpr uint32_t kCurThreadPhys = 0x0003da64;
            constexpr uint32_t kIdleThread = 0x8003d8f0;
            uint32_t cur_thread = 0;
            struct ThreadCount { uint32_t thread; uint64_t insns; };
            std::vector<ThreadCount> thread_counts;
            auto bump_thread = [&](uint32_t t, uint64_t n) {
                for (auto& tc : thread_counts)
                    if (tc.thread == t) { tc.insns += n; return; }
                thread_counts.push_back({t, n});
            };
            std::vector<uint32_t> nonidle_ring(512, 0);
            uint64_t nonidle_n = 0;
            if (!reduced_retire_diag) {
                mach.cpu().on_retired([&](const RetiredInsn& e) {
                    trace[trace_n++ & 63] = e.eip;
                    if ((trace_n & 63) == 0) {
                        uint32_t t = 0;
                        if (mach.read(kCurThreadPhys, &t, 4) &&
                            (t & 0x80000000u)) {
                            cur_thread = t;
                            bump_thread(t, 64);
                        }
                    }
                    if (cur_thread && cur_thread != kIdleThread)
                        nonidle_ring[nonidle_n++ & 511] = e.eip;
                    bool now_idle = in_idle(e.eip);
                    // Latch the FIRST entry into the idle loop — that's the boot thread's
                    // original block (it never runs again after this), so the history is
                    // its blocking path.
                    if (now_idle && !was_idle && !idle_latched) {
                        for (int i = 0; i < 64; ++i) pre_idle[i] = trace[i];
                        pre_idle_n = trace_n;
                        idle_latched = true;
                    }
                    was_idle = now_idle;
                });
            } else {
                std::printf(
                    "[2bl] reduced retire diagnostics enabled; exact PC/thread "
                    "history will be omitted\n");
            }
            // Guest-vector observability: log every guest-raised vector (capped), and decode
            // the kernel debug service (INT 0x2D, eax=1 -> DbgPrint with ecx=buffer) so the
            // kernel's own diagnostics reach our console.
            int vec_logged = 0;
            auto read_va_str = [&](uint32_t va, char* out, size_t n) {
                uint32_t pa = 0;
                if (!mach.cpu().translate(va, pa) || !mach.read(pa, out, n - 1)) return false;
                out[n - 1] = 0;
                for (char* c = out; *c; ++c)
                    if (*c < 0x20 || *c > 0x7e) *c = *c == '\n' ? ' ' : '.';
                return true;
            };
            mach.cpu().on_interrupt([&](uint32_t vec) {
                X86Regs ir = mach.cpu().get_regs();
                if (vec == 0x2d) {
                    // NT debug services: eax=1 print (ecx=buffer, edx=len), 2 prompt,
                    // 3 load symbols (ecx=STRING image name, edx=info), 4 unload symbols.
                    char msg[160] = {0};
                    if (ir.eax == 1) {
                        if (read_va_str(ir.ecx, msg, sizeof(msg)))
                            std::printf("[dbg] DbgPrint: %s\n", msg);
                        return;
                    }
                    if (ir.eax == 3 || ir.eax == 4) {
                        // ecx -> ANSI_STRING {USHORT len; USHORT max; PCHAR buf}
                        uint32_t pa = 0;
                        uint8_t s[8] = {0};
                        uint32_t buf_va = 0;
                        uint16_t len = 0;
                        if (mach.cpu().translate(ir.ecx, pa) && mach.read(pa, s, 8)) {
                            len = static_cast<uint16_t>(s[0] | (s[1] << 8));
                            buf_va = s[4] | (s[5] << 8) | (s[6] << 16) |
                                     (static_cast<uint32_t>(s[7]) << 24);
                        }
                        if (buf_va && len && len < sizeof(msg) &&
                            read_va_str(buf_va, msg, len + 1))
                            std::printf("[dbg] %s symbols: %s\n",
                                        ir.eax == 3 ? "LOAD" : "UNLOAD", msg);
                        else
                            std::printf("[dbg] %s symbols (name unreadable, ecx=0x%08x)\n",
                                        ir.eax == 3 ? "LOAD" : "UNLOAD", ir.ecx);
                        return;
                    }
                    static int svc_logged = 0;
                    if (svc_logged < 16) {
                        uint8_t raw[16] = {0};
                        uint32_t pa = 0;
                        if (mach.cpu().translate(ir.ecx, pa)) mach.read(pa, raw, 16);
                        std::printf("[dbg] debug service eax=%u ecx=0x%08x edx=0x%08x "
                                    "[ecx]=%02x%02x%02x%02x %02x%02x%02x%02x\n", ir.eax, ir.ecx,
                                    ir.edx, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
                                    raw[6], raw[7]);
                        ++svc_logged;
                    }
                    return;
                }
                if (vec_logged < 32) {
                    std::printf("[dbg] guest vector 0x%02x raised at eip=0x%08x eax=0x%08x\n", vec,
                                ir.eip, ir.eax);
                    if (++vec_logged == 32) std::printf("[dbg] (vector log capped)\n");
                }
            });
            X86Regs r{};
            r.eip = 0x901ee;
            r.esp = 0x00400000;  // scratch stack in RAM (2BL sets its own later)
            r.eflags = 0x2;
            mach.cpu().set_regs(r);
            mach.cpu().setup_flat_segments(0x1000);  // MCPX handoff GDT (code=0x08, data=0x10)
            xlle::perf::g_enabled = perf_accounting;
            std::printf("[2bl] entering decrypted 2BL at 0x901ee (budget %llu insns) ...\n",
                        (unsigned long long)max_insns);
            std::string err2;
            uint64_t n = mach.run(0x901ee, max_insns, err2);
            const bool host_memory_safety_stop =
                err2.find("host memory reached soft limit") != std::string::npos ||
                err2.find("GetProcessMemoryInfo failed while enforcing host memory limit") !=
                    std::string::npos;
            if (host_memory_safety_stop) {
                // Preserve the soft-to-hard-limit headroom. Full scanout, raster-state,
                // scheduler, and stack diagnostics allocate temporary vectors and can turn
                // a controlled memory stop into std::bad_alloc before teardown.
                const std::string perf_report = xlle::perf::format_report();
                if (!perf_report.empty()) std::printf("%s\n", perf_report.c_str());
                print_generic_combiner_perf_report();
                const std::string memory_report = xlle::perf::format_memory_report();
                if (!memory_report.empty()) std::printf("%s\n", memory_report.c_str());
                print_memory_diagnostics();
                host_input.close();
                const X86Regs stopped = mach.cpu().get_regs();
                std::printf(
                    "[2bl] retired %llu instructions; final eip=0x%08x eax=0x%08x "
                    "ecx=0x%08x\n",
                    static_cast<unsigned long long>(n), stopped.eip, stopped.eax,
                    stopped.ecx);
                std::printf("[2bl] STOPPED: %s\n", err2.c_str());
                std::printf(
                    "[host] allocation-heavy final diagnostics skipped after memory "
                    "safety stop\n");
                std::fflush(stdout);
                return 2;
            }
            // The run may cross its final series deadline after the last 1 kHz host poll.
            capture_frame_series();
            {
                const std::string perf_report = xlle::perf::format_report();
                if (!perf_report.empty()) std::printf("%s\n", perf_report.c_str());
                print_generic_combiner_perf_report();
                const std::string memory_report =
                    xlle::perf::format_memory_report();
                if (!memory_report.empty())
                    std::printf("%s\n", memory_report.c_str());
                print_memory_diagnostics();
            }
            host_input.close();
            if (test_synthetic_controller_hub_input_down) {
                const bool complete =
                    down_script.phase() == XidDownReportScript::Phase::kComplete;
                std::printf(
                    "[input] TEST-ONLY %s final-phase=%s insns=%llu endpoint2=%llu "
                    "aggregate=%llu configured=%u\n",
                    complete ? "COMPLETE" : "NOT-DELIVERED",
                    XidDownReportScript::phase_name(down_script.phase()),
                    static_cast<unsigned long long>(mach.total_insns()),
                    static_cast<unsigned long long>(
                        mach.ohci0()->xid_endpoint2_input_reports()),
                    static_cast<unsigned long long>(mach.ohci0()->xid_input_reports()),
                    mach.ohci0()->xid_configuration());
            } else if (test_synthetic_controller_hub_input_down_a) {
                const bool complete = down_a_script.phase() ==
                    XidDownThenAReportScript::Phase::kComplete;
                std::printf(
                    "[input] TEST-ONLY %s Down-then-A final-phase=%s insns=%llu "
                    "endpoint2=%llu aggregate=%llu configured=%u\n",
                    complete ? "COMPLETE" : "NOT-DELIVERED",
                    XidDownThenAReportScript::phase_name(down_a_script.phase()),
                    static_cast<unsigned long long>(mach.total_insns()),
                    static_cast<unsigned long long>(
                        mach.ohci0()->xid_endpoint2_input_reports()),
                    static_cast<unsigned long long>(mach.ohci0()->xid_input_reports()),
                    mach.ohci0()->xid_configuration());
            } else if (test_input_script) {
                const bool complete =
                    generic_script.phase() == GenericInputScript::Phase::kComplete;
                std::printf(
                    "[input] TEST-ONLY %s generic script final-phase=%s event=%zu insns=%llu "
                    "endpoint2=%llu aggregate=%llu configured=%u\n",
                    complete ? "COMPLETE" : "NOT-DELIVERED",
                    GenericInputScript::phase_name(generic_script.phase()),
                    generic_script.index(),
                    static_cast<unsigned long long>(mach.total_insns()),
                    static_cast<unsigned long long>(
                        mach.ohci0()->xid_endpoint2_input_reports()),
                    static_cast<unsigned long long>(mach.ohci0()->xid_input_reports()),
                    mach.ohci0()->xid_configuration());
            }
            X86Regs f = mach.cpu().get_regs();
            Nv2a::ScanoutInfo scanout;
            std::vector<uint8_t> frame_rgba;
            std::string scanout_error;
            const bool frame_available =
                mach.nv2a() &&
                mach.nv2a()->capture_scanout(frame_rgba, scanout, scanout_error);
            bool frame_output_ok = true;
            std::printf("[2bl] retired %llu instructions; final eip=0x%08x eax=0x%08x ecx=0x%08x\n",
                        (unsigned long long)n, f.eip, f.eax, f.ecx);
            if (!err2.empty()) std::printf("[2bl] STOPPED: %s\n", err2.c_str());
            else std::printf("[2bl] ran to instruction budget without fault\n");
            // Dump kernel code bytes around the final EIP (virtual 0x8xxxxxxx -> physical
            // & 0x07FFFFFF) so the faulting instruction can be disassembled off-line.
            {
                uint32_t peip = f.eip & 0x07FFFFFF;
                uint8_t code[48] = {0};
                if (peip >= 24 && mach.read(peip - 24, code, sizeof(code))) {
                    std::printf("[2bl] code @0x%08x-0x%08x:", peip - 24, peip + 24);
                    for (int i = 0; i < 48; ++i) std::printf(" %02x", code[i]);
                    std::printf("\n      (EIP is the 25th byte above)\n");
                }
            }
            if (!reduced_retire_diag) {
                std::printf("[2bl] last PCs:");
                uint64_t first = trace_n > 16 ? trace_n - 16 : 0;
                for (uint64_t i = first; i < trace_n; ++i)
                    std::printf(" %05x", trace[i & 15]);
                std::printf("\n");
            }
            std::printf("[2bl] regs: eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x "
                        "ebp=%08x esp=%08x\n", f.eax, f.ebx, f.ecx, f.edx, f.esi, f.edi, f.ebp,
                        f.esp);
            auto dump_va = [&](const char* tag, uint32_t va, int n) {
                uint32_t pa = 0;
                uint8_t buf[64] = {0};
                if (n > 64) n = 64;
                if (mach.cpu().translate(va, pa) && mach.read(pa, buf, n)) {
                    std::printf("[2bl] %s VA=%08x PA=%08x:", tag, va, pa);
                    for (int i = 0; i < n; ++i) std::printf(" %02x", buf[i]);
                    std::printf("\n");
                }
            };
            dump_va("[ecx]", f.ecx, 32);
            dump_va("[esi]", f.esi, 32);
            dump_va("[esp]", f.esp, 64);
            dump_va("[ebp]", f.ebp, 64);
            {
                uint32_t stack[16] = {0};
                uint32_t stack_pa = 0;
                std::vector<uint32_t> dumped;
                if (mach.cpu().translate(f.esp, stack_pa) &&
                    mach.read(stack_pa, stack, sizeof(stack))) {
                    for (uint32_t candidate : stack) {
                        if (candidate < 0x80010000u || candidate >= 0x81000000u ||
                            std::find(dumped.begin(), dumped.end(), candidate) != dumped.end())
                            continue;
                        uint32_t code_pa = 0;
                        uint8_t code[48] = {0};
                        if (!mach.cpu().translate(candidate - 24, code_pa) ||
                            !mach.read(code_pa, code, sizeof(code)))
                            continue;
                        dumped.push_back(candidate);
                        std::printf("[2bl] stack code @VA=%08x PA=%08x:", candidate, code_pa + 24);
                        for (uint8_t byte : code) std::printf(" %02x", byte);
                        std::printf("\n      (candidate return address is the 25th byte above)\n");
                    }
                }
            }
            // Dump kernel scheduler state the idle loop watches (ebx≈PRCB, ebp≈DPC list head).
            // Kernel VAs are identity-mapped 0x80000000->0x0, so physical = VA & 0x07FFFFFF.
            auto kdump = [&](const char* tag, uint32_t va, int n) {
                if (!(va & 0x80000000u)) return;
                uint8_t buf[64] = {0};
                if (n > 64) n = 64;
                if (mach.read(va & 0x07FFFFFFu, buf, n)) {
                    std::printf("[2bl] %s @%08x:", tag, va);
                    for (int i = 0; i < n; ++i) std::printf(" %02x", buf[i]);
                    std::printf("\n");
                }
            };
            kdump("PRCB(ebx)", f.ebx, 64);
            kdump("DPCq(ebp)", f.ebp, 16);
            // Scan kernel RAM for KTHREAD dispatcher headers (Type=6, Size=0x44 -> "06 00 44 00")
            // to enumerate every thread and its State (+0x2c) / Priority (+0x32). This tells us
            // whether a phase-1/dashboard work thread exists besides the running idle thread.
            {
                const uint32_t base = 0x1000, len = 0x3FF0000;  // ~64 MB (all of RAM)
                std::vector<uint8_t> km(len);
                mach.read(base, km.data(), km.size());
                std::printf("[2bl] KTHREAD scan (State 1=Ready 2=Running 5=Waiting):\n");
                int found = 0;
                std::vector<uint32_t> waiters;
                for (size_t o = 0; o + 0x60 < km.size(); o += 4) {
                    // KTHREAD dispatcher header: Type byte = 6 (ThreadObject), Size byte = 0x44.
                    if (km[o] == 0x06 && km[o + 2] == 0x44 && km[o + 1] == 0 && km[o + 3] == 0) {
                        uint32_t va = 0x80000000u + base + static_cast<uint32_t>(o);
                        uint8_t state = km[o + 0x2c];
                        std::printf("[2bl]   KTHREAD=%08x State=%u Priority=%d WaitReason=%u\n", va,
                                    state, static_cast<int8_t>(km[o + 0x32]), km[o + 0x57]);
                        if (state == 5) waiters.push_back(va);  // Waiting
                        if (++found >= 16) break;
                    }
                }
                std::printf("[2bl] total KTHREADs found: %d\n", found);
                // Follow every waiting thread's wait block to the object it blocks on.
                for (uint32_t waiter : waiters) {
                    for (int r = 0; r < 128; r += 32) {  // raw KTHREAD dump for offset validation
                        uint8_t b[32] = {0};
                        mach.read((waiter + r) & 0x07FFFFFFu, b, 32);
                        std::printf("[2bl]   KT+%02x:", r);
                        for (int i = 0; i < 32; ++i) std::printf(" %02x", b[i]);
                        std::printf("\n");
                    }
                    // The wait block lives on the thread's kernel stack (0xD0000000 region), a
                    // non-identity mapping — translate through the page tables to read it.
                    auto read_va = [&](uint32_t va, void* buf, size_t n) {
                        uint32_t pa = 0;
                        return mach.cpu().translate(va, pa) && mach.read(pa, buf, n);
                    };
                    uint32_t wbl = 0, obj = 0, nextwb = 0;
                    mach.read((waiter + 0x58) & 0x07FFFFFFu, &wbl, 4);      // WaitBlockList
                    if (wbl & 0x80000000u) {
                        read_va(wbl + 0x0c, &obj, 4);     // KWAIT_BLOCK.Object (+0x08 is Thread)
                        read_va(wbl + 0x10, &nextwb, 4);  // NextWaitBlock
                    }
                    std::printf("[2bl] waiter %08x WaitBlockList=%08x Object=%08x nextWB=%08x\n",
                                waiter, wbl, obj, nextwb);
                    // Dump the waiter's kernel stack (saved ESP = KernelStack @+0x24) as dwords,
                    // to reconstruct its call chain — return addresses are kernel code pointers.
                    uint32_t ksp = 0;
                    mach.read((waiter + 0x24) & 0x07FFFFFFu, &ksp, 4);
                    for (int r = 0; r < 0x120; r += 32) {
                        uint8_t b[32] = {0};
                        if (!read_va(ksp + r, b, 32)) break;
                        std::printf("[2bl] stk %08x:", ksp + r);
                        for (int i = 0; i < 32; i += 4)
                            std::printf(" %02x%02x%02x%02x", b[i + 3], b[i + 2], b[i + 1], b[i]);
                        std::printf("\n");
                    }
                    if (obj & 0x80000000u) {
                        uint8_t hdr[16] = {0};
                        read_va(obj, hdr, 16);
                        int32_t sig = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (hdr[7] << 24);
                        std::printf("[2bl] wait object %08x: Type=%u Size=%u SignalState=%d "
                                    "(Type 6=Thread -> waiting for it to terminate)\n", obj,
                                    hdr[0], hdr[2], sig);
                        uint32_t po = 0, pi = 0, pp = 0;
                        mach.cpu().translate(obj, po);
                        mach.cpu().translate(0x8003d8f0, pi);
                        mach.cpu().translate(0x80001bf8, pp);
                        std::printf("[2bl] phys: waitobj=%08x idleThr(8003d8f0)=%08x "
                                    "phase1(80001bf8)=%08x\n", po, pi, pp);
                    }
                }
            }
            // The boot thread's last PCs before its final block-and-yield-to-idle.
            if (!reduced_retire_diag) {
                std::printf("[2bl] pre-idle PCs (last real work before blocking):");
                uint64_t pf = pre_idle_n > 64 ? pre_idle_n - 64 : 0;
                for (uint64_t i = pf; i < pre_idle_n; ++i)
                    std::printf(" %08x", pre_idle[i & 63]);
                std::printf("\n");
                // Per-thread attribution + the non-idle threads' final instructions.
                std::printf("[2bl] insns by thread (sampled/64):");
                for (const auto& tc : thread_counts)
                    std::printf(" %08x=%llu", tc.thread,
                                (unsigned long long)tc.insns);
                std::printf("\n[2bl] last %llu non-idle PCs:",
                            (unsigned long long)(nonidle_n < 96 ? nonidle_n : 96));
                {
                    uint64_t nf = nonidle_n > 96 ? nonidle_n - 96 : 0;
                    for (uint64_t i = nf; i < nonidle_n; ++i)
                        std::printf(" %08x", nonidle_ring[i & 511]);
                }
                std::printf("\n");
            }
            if (mach.nv2a()) {
                std::printf("[2bl] NV2A: vblanks=%llu pmc_intr_en=0x%08x pcrtc_intr_en=0x%08x\n",
                            (unsigned long long)mach.nv2a()->vblanks(),
                            mach.nv2a()->pmc_intr_en(), mach.nv2a()->pcrtc_intr_en());
                std::printf("[2bl] NV2A PFIFO: methods=%llu semaphore-writes=%llu\n",
                            (unsigned long long)mach.nv2a()->push_methods(),
                            (unsigned long long)mach.nv2a()->semaphore_writes());
                std::printf("[2bl] NV2A draws: begin/end=%llu elem16/32=%llu/%llu "
                            "arrays=%llu inline=%llu\n",
                            (unsigned long long)mach.nv2a()->method_count(0x17FC),
                            (unsigned long long)mach.nv2a()->method_count(0x1800),
                            (unsigned long long)mach.nv2a()->method_count(0x1808),
                            (unsigned long long)mach.nv2a()->method_count(0x1810),
                            (unsigned long long)mach.nv2a()->method_count(0x1818));
                std::printf("[2bl] NV2A classes: Kelvin97=%llu surfaces62=%llu "
                            "blit9f=%llu pattern44=%llu unbound=%llu\n",
                            (unsigned long long)mach.nv2a()->class_method_count(0x97),
                            (unsigned long long)mach.nv2a()->class_method_count(0x62),
                            (unsigned long long)mach.nv2a()->class_method_count(0x9F),
                            (unsigned long long)mach.nv2a()->class_method_count(0x44),
                            (unsigned long long)mach.nv2a()->class_method_count(0));
                std::printf("[2bl] NV2A raster: color-clears/pixels=%llu/%llu "
                            "last=%08x zeta-clears/pixels=%llu/%llu last=%08x "
                            "depth-batches/tri/frags/pass/writes=%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->color_clears(),
                            (unsigned long long)mach.nv2a()->color_clear_pixels(),
                            mach.nv2a()->last_color_clear_address(),
                            (unsigned long long)mach.nv2a()->zeta_clears(),
                            (unsigned long long)mach.nv2a()->zeta_clear_pixels(),
                            mach.nv2a()->last_zeta_clear_address(),
                            (unsigned long long)mach.nv2a()->depth_raster_batches(),
                            (unsigned long long)mach.nv2a()->rasterized_triangles(),
                            (unsigned long long)mach.nv2a()->rasterized_fragments(),
                            (unsigned long long)mach.nv2a()->depth_test_passes(),
                            (unsigned long long)mach.nv2a()->depth_writes(),
                            mach.nv2a()->last_raster_address(),
                            (unsigned long long)mach.nv2a()->raster_output_hash());
                std::printf("[2bl] NV2A radial-color: batches/tri/frags/pass/"
                            "zwrite/cwrite=%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->radial_color_batches(),
                            (unsigned long long)mach.nv2a()->radial_color_triangles(),
                            (unsigned long long)mach.nv2a()->radial_color_fragments(),
                            (unsigned long long)mach.nv2a()->radial_color_depth_passes(),
                            (unsigned long long)mach.nv2a()->radial_color_depth_writes(),
                            (unsigned long long)mach.nv2a()->radial_color_writes(),
                            mach.nv2a()->last_color_raster_address(),
                            (unsigned long long)mach.nv2a()->radial_color_output_hash());
                std::printf("[2bl] NV2A textured-color: batches/tri/frags/pass/"
                            "zwrite/cwrite/samples=%llu/%llu/%llu/%llu/%llu/"
                            "%llu/%llu last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->textured_color_batches(),
                            (unsigned long long)mach.nv2a()->textured_color_triangles(),
                            (unsigned long long)mach.nv2a()->textured_color_fragments(),
                            (unsigned long long)mach.nv2a()->textured_color_depth_passes(),
                            (unsigned long long)mach.nv2a()->textured_color_depth_writes(),
                            (unsigned long long)mach.nv2a()->textured_color_writes(),
                            (unsigned long long)mach.nv2a()->texture_samples(),
                            mach.nv2a()->last_textured_raster_address(),
                            (unsigned long long)mach.nv2a()->textured_color_output_hash());
                std::printf("[2bl] NV2A pitch-textured: batches/tri/frags/pass/"
                            "zwrite/cwrite/samples=%llu/%llu/%llu/%llu/%llu/"
                            "%llu/%llu last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->pitch_textured_color_batches(),
                            (unsigned long long)mach.nv2a()->pitch_textured_color_triangles(),
                            (unsigned long long)mach.nv2a()->pitch_textured_color_fragments(),
                            (unsigned long long)mach.nv2a()->pitch_textured_color_depth_passes(),
                            (unsigned long long)mach.nv2a()->pitch_textured_color_depth_writes(),
                            (unsigned long long)mach.nv2a()->pitch_textured_color_writes(),
                            (unsigned long long)mach.nv2a()->pitch_texture_samples(),
                            mach.nv2a()->last_pitch_textured_raster_address(),
                            (unsigned long long)mach.nv2a()->pitch_textured_color_output_hash());
                std::printf("[2bl] NV2A projected-color: batches/tri/frags/pass/"
                            "zwrite/cwrite/samples=%llu/%llu/%llu/%llu/%llu/"
                            "%llu/%llu last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->projected_color_batches(),
                            (unsigned long long)mach.nv2a()->projected_color_triangles(),
                            (unsigned long long)mach.nv2a()->projected_color_fragments(),
                            (unsigned long long)mach.nv2a()->projected_color_depth_passes(),
                            (unsigned long long)mach.nv2a()->projected_color_depth_writes(),
                            (unsigned long long)mach.nv2a()->projected_color_writes(),
                            (unsigned long long)mach.nv2a()->projected_texture_samples(),
                            mach.nv2a()->last_projected_raster_address(),
                            (unsigned long long)mach.nv2a()->projected_color_output_hash());
                std::printf("[2bl] NV2A pitch-projected: batches/tri/frags/pass/"
                            "zwrite/cwrite/samples=%llu/%llu/%llu/%llu/%llu/"
                            "%llu/%llu last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->pitch_projected_color_batches(),
                            (unsigned long long)mach.nv2a()->pitch_projected_color_triangles(),
                            (unsigned long long)mach.nv2a()->pitch_projected_color_fragments(),
                            (unsigned long long)mach.nv2a()->pitch_projected_color_depth_passes(),
                            (unsigned long long)mach.nv2a()->pitch_projected_color_depth_writes(),
                            (unsigned long long)mach.nv2a()->pitch_projected_color_writes(),
                            (unsigned long long)mach.nv2a()->pitch_projected_texture_samples(),
                            mach.nv2a()->last_pitch_projected_raster_address(),
                            (unsigned long long)mach.nv2a()->pitch_projected_color_output_hash());
                std::printf("[2bl] NV2A pitch-single: batches/tri/frags/pass/"
                            "cwrite/samples=%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->pitch_single_texture_batches(),
                            (unsigned long long)mach.nv2a()->pitch_single_texture_triangles(),
                            (unsigned long long)mach.nv2a()->pitch_single_texture_fragments(),
                            (unsigned long long)mach.nv2a()->pitch_single_texture_depth_passes(),
                            (unsigned long long)mach.nv2a()->pitch_single_texture_writes(),
                            (unsigned long long)mach.nv2a()->pitch_single_texture_samples(),
                            mach.nv2a()->last_pitch_single_texture_raster_address(),
                            (unsigned long long)mach.nv2a()->pitch_single_texture_output_hash());
                std::printf("[2bl] NV2A tex7-fan: batches/tri/frags/pass/"
                            "cwrite/samples=%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->tex7_fan_batches(),
                            (unsigned long long)mach.nv2a()->tex7_fan_triangles(),
                            (unsigned long long)mach.nv2a()->tex7_fan_fragments(),
                            (unsigned long long)mach.nv2a()->tex7_fan_depth_passes(),
                            (unsigned long long)mach.nv2a()->tex7_fan_writes(),
                            (unsigned long long)mach.nv2a()->tex7_fan_samples(),
                            mach.nv2a()->last_tex7_fan_raster_address(),
                            (unsigned long long)mach.nv2a()->tex7_fan_output_hash());
                std::printf("[2bl] NV2A pitch-multi: batches/tri/frags/depth/"
                            "alpha/zwrite/cwrite/samples="
                            "%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->pitch_multi_texture_batches(),
                            (unsigned long long)mach.nv2a()->pitch_multi_texture_triangles(),
                            (unsigned long long)mach.nv2a()->pitch_multi_texture_fragments(),
                            (unsigned long long)mach.nv2a()->pitch_multi_texture_depth_passes(),
                            (unsigned long long)mach.nv2a()->pitch_multi_texture_alpha_passes(),
                            (unsigned long long)mach.nv2a()->pitch_multi_texture_depth_writes(),
                            (unsigned long long)mach.nv2a()->pitch_multi_texture_color_writes(),
                            (unsigned long long)mach.nv2a()->pitch_multi_texture_samples(),
                            mach.nv2a()->last_pitch_multi_texture_raster_address(),
                            (unsigned long long)mach.nv2a()->pitch_multi_texture_output_hash());
                std::printf("[2bl] NV2A pitch-three: batches/tri/frags/depth/"
                            "zwrite/cwrite/samples/zero0/zero1/rescale0/rescale1/tie0/tie1="
                            "%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x zero-factor3=%08x rescale-factor3=%08x tie-factor3=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->pitch_three_texture_batches(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_triangles(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_fragments(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_depth_passes(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_depth_writes(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_color_writes(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_samples(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_zero_direction0(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_zero_direction1(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_rescale_direction0(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_rescale_direction1(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_xy_tie_direction0(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_xy_tie_direction1(),
                            mach.nv2a()->last_pitch_three_texture_raster_address(),
                            mach.nv2a()->last_pitch_three_zero_factor3(),
                            mach.nv2a()->last_pitch_three_rescale_factor3(),
                            mach.nv2a()->last_pitch_three_xy_tie_factor3(),
                            (unsigned long long)mach.nv2a()->pitch_three_texture_output_hash());
                std::printf("[2bl] NV2A pitch-tex5: batches/tri/frags/depth/"
                            "zwrite/cwrite/samples="
                            "%llu/%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->pitch_tex5_batches(),
                            (unsigned long long)mach.nv2a()->pitch_tex5_triangles(),
                            (unsigned long long)mach.nv2a()->pitch_tex5_fragments(),
                            (unsigned long long)mach.nv2a()->pitch_tex5_depth_passes(),
                            (unsigned long long)mach.nv2a()->pitch_tex5_depth_writes(),
                            (unsigned long long)mach.nv2a()->pitch_tex5_color_writes(),
                            (unsigned long long)mach.nv2a()->pitch_tex5_samples(),
                            mach.nv2a()->last_pitch_tex5_raster_address(),
                            (unsigned long long)mach.nv2a()->pitch_tex5_output_hash());
                std::printf("[2bl] NV2A tex=f fan: batches/tri/frags/alpha/"
                            "writes/samples=%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->texf_projected_fan_batches(),
                            (unsigned long long)mach.nv2a()->texf_projected_fan_triangles(),
                            (unsigned long long)mach.nv2a()->texf_projected_fan_fragments(),
                            (unsigned long long)mach.nv2a()->texf_projected_fan_alpha_passes(),
                            (unsigned long long)mach.nv2a()->texf_projected_fan_writes(),
                            (unsigned long long)mach.nv2a()->texf_projected_fan_samples(),
                            mach.nv2a()->last_texf_projected_fan_raster_address(),
                            (unsigned long long)mach.nv2a()->texf_projected_fan_output_hash());
                std::printf("[2bl] NV2A inline-blit: "
                            "batches/tri/frags/writes/samples="
                            "%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->inline_linear_blit_batches(),
                            (unsigned long long)mach.nv2a()->inline_linear_blit_triangles(),
                            (unsigned long long)mach.nv2a()->inline_linear_blit_fragments(),
                            (unsigned long long)mach.nv2a()->inline_linear_blit_writes(),
                            (unsigned long long)mach.nv2a()->inline_linear_blit_samples(),
                            mach.nv2a()->last_inline_linear_blit_raster_address(),
                            (unsigned long long)mach.nv2a()->inline_linear_blit_output_hash());
                std::printf("[2bl] NV2A inline-color: batches/tri/frags/alpha/"
                            "writes/samples=%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->inline_color_batches(),
                            (unsigned long long)mach.nv2a()->inline_color_triangles(),
                            (unsigned long long)mach.nv2a()->inline_color_fragments(),
                            (unsigned long long)mach.nv2a()->inline_color_alpha_passes(),
                            (unsigned long long)mach.nv2a()->inline_color_writes(),
                            (unsigned long long)mach.nv2a()->inline_texture_samples(),
                            mach.nv2a()->last_inline_raster_address(),
                            (unsigned long long)mach.nv2a()->inline_color_output_hash());
                std::printf("[2bl] NV2A generic-combiner: "
                            "attempted/accelerated/fallback/software/exec/tri/frags/depth-pass/writes/tex-samples="
                            "%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx\n",
                            (unsigned long long)mach.nv2a()->generic_combiner_attempted_batches(),
                            (unsigned long long)mach.nv2a()->generic_combiner_accelerated_batches(),
                            (unsigned long long)mach.nv2a()->generic_combiner_fallback_batches(),
                            (unsigned long long)mach.nv2a()->generic_combiner_batches(),
                            (unsigned long long)mach.nv2a()->generic_combiner_executed_batches(),
                            (unsigned long long)mach.nv2a()->generic_combiner_triangles(),
                            (unsigned long long)mach.nv2a()->generic_combiner_fragments(),
                            (unsigned long long)mach.nv2a()->generic_combiner_depth_passes(),
                            (unsigned long long)mach.nv2a()->generic_combiner_writes(),
                            (unsigned long long)mach.nv2a()->generic_combiner_texture_samples(),
                            mach.nv2a()->last_generic_combiner_raster_address(),
                            (unsigned long long)mach.nv2a()->generic_combiner_output_hash());
                std::printf("[2bl] NV2A generic-combiner resident: "
                            "candidates/supported/uploads/reuses/target-switch-resolves/submits="
                            "%llu/%llu/%llu/%llu/%llu/%llu\n",
                            (unsigned long long)mach.nv2a()->generic_combiner_resident_candidates(),
                            (unsigned long long)mach.nv2a()->generic_combiner_resident_supported(),
                            (unsigned long long)mach.nv2a()->generic_combiner_resident_uploads(),
                            (unsigned long long)mach.nv2a()->generic_combiner_resident_reuses(),
                            (unsigned long long)mach.nv2a()->generic_combiner_resident_target_switch_resolves(),
                            (unsigned long long)mach.nv2a()->generic_combiner_resident_submits());
                std::printf("[2bl] NV2A generic-combiner resident rejects:");
                bool any_resident_reject = false;
                for (size_t reason = 0;
                     reason < xlle::Nv2a::generic_resident_reject_reason_count();
                     ++reason) {
                    const uint64_t count =
                        mach.nv2a()->generic_combiner_resident_reject_batches(reason);
                    if (!count) continue;
                    std::printf(" %s=%llu",
                                xlle::Nv2a::generic_resident_reject_reason_name(reason),
                                (unsigned long long)count);
                    any_resident_reject = true;
                }
                if (!any_resident_reject) std::printf(" none");
                std::printf("\n");
                std::printf("[2bl] NV2A generic-combiner texture-modulate: "
                            "observed/exact=%llu/%llu rejects=",
                            (unsigned long long)mach.nv2a()
                                ->generic_combiner_texture_modulate_observed(),
                            (unsigned long long)mach.nv2a()
                                ->generic_combiner_texture_modulate_exact());
                bool any_texture_modulate_reject = false;
                for (size_t reason = 0;
                     reason <
                     xlle::Nv2a::generic_texture_modulate_reject_reason_count();
                     ++reason) {
                    const uint64_t count = mach.nv2a()
                        ->generic_combiner_texture_modulate_reject_batches(reason);
                    if (!count) continue;
                    if (any_texture_modulate_reject) std::printf(",");
                    std::printf("%s:%llu",
                                xlle::Nv2a::
                                    generic_texture_modulate_reject_reason_name(reason),
                                (unsigned long long)count);
                    any_texture_modulate_reject = true;
                }
                if (!any_texture_modulate_reject) std::printf("none");
                std::printf("\n");
                std::printf("[2bl] NV2A generic-combiner skip census:");
                for (size_t reason = 0;
                     reason < xlle::Nv2a::generic_skip_reason_count(); ++reason) {
                    const uint64_t count = mach.nv2a()->generic_skip_reason_batches(reason);
                    if (count) {
                        std::printf(" %s=%llu",
                                   xlle::Nv2a::generic_skip_reason_name(reason),
                                   (unsigned long long)count);
                    }
                }
                std::printf("\n");
                for (size_t reason = 0;
                     reason < xlle::Nv2a::generic_skip_reason_count(); ++reason) {
                    std::vector<std::pair<uint64_t, uint64_t>> details;
                    mach.nv2a()->generic_skip_detail_census(
                        static_cast<xlle::Nv2a::GenericSkipReason>(reason), details);
                    if (details.empty()) continue;
                    std::sort(details.begin(), details.end(),
                             [](const auto& a, const auto& b) {
                                 return a.second > b.second;
                             });
                    std::printf("[2bl] NV2A generic-combiner skip detail %s:",
                               xlle::Nv2a::generic_skip_reason_name(reason));
                    const size_t detail_limit = std::min<size_t>(details.size(), 16);
                    for (size_t i = 0; i < detail_limit; ++i) {
                        std::printf(" %016llx:%llu",
                                   (unsigned long long)details[i].first,
                                   (unsigned long long)details[i].second);
                    }
                    std::printf("\n");
                }
                {
                    std::vector<std::pair<uint64_t, uint64_t>> generic_fingerprints;
                    mach.nv2a()->generic_combiner_fingerprint_census(generic_fingerprints);
                    std::sort(generic_fingerprints.begin(), generic_fingerprints.end(),
                             [](const auto& a, const auto& b) {
                                 return a.second > b.second;
                             });
                    std::printf("[2bl] NV2A generic-combiner fingerprints: unique=%zu",
                               generic_fingerprints.size());
                    const size_t fp_limit =
                        std::min<size_t>(generic_fingerprints.size(), 16);
                    for (size_t i = 0; i < fp_limit; ++i) {
                        std::printf(" %016llx:%llu",
                                   (unsigned long long)generic_fingerprints[i].first,
                                   (unsigned long long)generic_fingerprints[i].second);
                    }
                    std::printf("\n");
                }
                std::printf("[2bl] NV2A immediate-quad A4R4G4B4: "
                            "batches/tri/frags/alpha/writes/samples="
                            "%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx candidates=%llu "
                            "reject-mask=%02x raster-reject=%06x\n",
                            (unsigned long long)mach.nv2a()->immediate_quad_raster_batches(),
                            (unsigned long long)mach.nv2a()->immediate_quad_raster_triangles(),
                            (unsigned long long)mach.nv2a()->immediate_quad_raster_fragments(),
                            (unsigned long long)mach.nv2a()->immediate_quad_raster_alpha_passes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_raster_writes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_texture_samples(),
                            mach.nv2a()->last_immediate_quad_raster_address(),
                            (unsigned long long)mach.nv2a()->immediate_quad_raster_output_hash(),
                            (unsigned long long)mach.nv2a()->immediate_quad_rs12_candidates(),
                            mach.nv2a()->last_immediate_quad_rs12_reject_mask(),
                            mach.nv2a()->last_immediate_quad_rs12_raster_reject_mask());
                std::printf("[2bl] NV2A immediate-quad I8+palette: "
                            "batches/tri/frags/alpha/writes/samples="
                            "%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx candidates=%llu "
                            "reject-mask=%02x raster-reject=%06x\n",
                            (unsigned long long)mach.nv2a()->immediate_quad_i8_raster_batches(),
                            (unsigned long long)mach.nv2a()->immediate_quad_i8_raster_triangles(),
                            (unsigned long long)mach.nv2a()->immediate_quad_i8_raster_fragments(),
                            (unsigned long long)mach.nv2a()->immediate_quad_i8_raster_alpha_passes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_i8_raster_writes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_i8_texture_samples(),
                            mach.nv2a()->last_immediate_quad_i8_raster_address(),
                            (unsigned long long)mach.nv2a()->immediate_quad_i8_raster_output_hash(),
                            (unsigned long long)mach.nv2a()->immediate_quad_i8_candidates(),
                            mach.nv2a()->last_immediate_quad_i8_reject_mask(),
                            mach.nv2a()->last_immediate_quad_i8_raster_reject_mask());
                std::printf("[2bl] NV2A immediate-quad A8R8G8B8: "
                            "batches/tri/frags/alpha/writes/samples="
                            "%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx candidates=%llu "
                            "reject-mask=%02x raster-reject=%06x\n",
                            (unsigned long long)mach.nv2a()->immediate_quad_a8_raster_batches(),
                            (unsigned long long)mach.nv2a()->immediate_quad_a8_raster_triangles(),
                            (unsigned long long)mach.nv2a()->immediate_quad_a8_raster_fragments(),
                            (unsigned long long)mach.nv2a()->immediate_quad_a8_raster_alpha_passes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_a8_raster_writes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_a8_texture_samples(),
                            mach.nv2a()->last_immediate_quad_a8_raster_address(),
                            (unsigned long long)mach.nv2a()->immediate_quad_a8_raster_output_hash(),
                            (unsigned long long)mach.nv2a()->immediate_quad_a8_candidates(),
                            mach.nv2a()->last_immediate_quad_a8_reject_mask(),
                            mach.nv2a()->last_immediate_quad_a8_raster_reject_mask());
                std::printf("[2bl] NV2A immediate-quad menu I8+palette 64x128: "
                            "batches/tri/frags/alpha/writes/samples="
                            "%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx candidates=%llu "
                            "reject-mask=%02x raster-reject=%06x\n",
                            (unsigned long long)mach.nv2a()->immediate_quad_menu_i8_raster_batches(),
                            (unsigned long long)mach.nv2a()->immediate_quad_menu_i8_raster_triangles(),
                            (unsigned long long)mach.nv2a()->immediate_quad_menu_i8_raster_fragments(),
                            (unsigned long long)mach.nv2a()->immediate_quad_menu_i8_raster_alpha_passes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_menu_i8_raster_writes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_menu_i8_texture_samples(),
                            mach.nv2a()->last_immediate_quad_menu_i8_raster_address(),
                            (unsigned long long)mach.nv2a()->immediate_quad_menu_i8_raster_output_hash(),
                            (unsigned long long)mach.nv2a()->immediate_quad_menu_i8_candidates(),
                            mach.nv2a()->last_immediate_quad_menu_i8_reject_mask(),
                            mach.nv2a()->last_immediate_quad_menu_i8_raster_reject_mask());
                std::printf("[2bl] NV2A immediate-quad artwork I8+palette 512x128: "
                            "batches/tri/frags/alpha/writes/samples="
                            "%llu/%llu/%llu/%llu/%llu/%llu "
                            "last=%08x hash=%016llx candidates=%llu "
                            "reject-mask=%02x raster-reject=%06x\n",
                            (unsigned long long)mach.nv2a()->immediate_quad_artwork_i8_raster_batches(),
                            (unsigned long long)mach.nv2a()->immediate_quad_artwork_i8_raster_triangles(),
                            (unsigned long long)mach.nv2a()->immediate_quad_artwork_i8_raster_fragments(),
                            (unsigned long long)mach.nv2a()->immediate_quad_artwork_i8_raster_alpha_passes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_artwork_i8_raster_writes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_artwork_i8_texture_samples(),
                            mach.nv2a()->last_immediate_quad_artwork_i8_raster_address(),
                            (unsigned long long)mach.nv2a()->immediate_quad_artwork_i8_raster_output_hash(),
                            (unsigned long long)mach.nv2a()->immediate_quad_artwork_i8_candidates(),
                            mach.nv2a()->last_immediate_quad_artwork_i8_reject_mask(),
                            mach.nv2a()->last_immediate_quad_artwork_i8_raster_reject_mask());
                std::printf("[2bl] NV2A immediate-quad main-panel A8R8G8B8 512x512: "
                            "batches/tri/frags/alpha/writes/samples="
                            "%llu/%llu/%llu/%llu/%llu/%llu last=%08x hash=%016llx candidates=%llu reject-mask=%02x raster-reject=%06x\n",
                            (unsigned long long)mach.nv2a()->immediate_quad_main_panel_raster_batches(),
                            (unsigned long long)mach.nv2a()->immediate_quad_main_panel_raster_triangles(),
                            (unsigned long long)mach.nv2a()->immediate_quad_main_panel_raster_fragments(),
                            (unsigned long long)mach.nv2a()->immediate_quad_main_panel_raster_alpha_passes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_main_panel_raster_writes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_main_panel_texture_samples(),
                            mach.nv2a()->last_immediate_quad_main_panel_raster_address(),
                            (unsigned long long)mach.nv2a()->immediate_quad_main_panel_raster_output_hash(),
                            (unsigned long long)mach.nv2a()->immediate_quad_main_panel_candidates(),
                            mach.nv2a()->last_immediate_quad_main_panel_reject_mask(),
                            mach.nv2a()->last_immediate_quad_main_panel_raster_reject_mask());
                std::printf("[2bl] NV2A immediate-quad full-screen A8R8G8B8 1024x1024: "
                            "batches/tri/frags/alpha/writes/samples="
                            "%llu/%llu/%llu/%llu/%llu/%llu last=%08x hash=%016llx candidates=%llu reject-mask=%02x raster-reject=%06x\n",
                            (unsigned long long)mach.nv2a()->immediate_quad_fullscreen_raster_batches(),
                            (unsigned long long)mach.nv2a()->immediate_quad_fullscreen_raster_triangles(),
                            (unsigned long long)mach.nv2a()->immediate_quad_fullscreen_raster_fragments(),
                            (unsigned long long)mach.nv2a()->immediate_quad_fullscreen_raster_alpha_passes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_fullscreen_raster_writes(),
                            (unsigned long long)mach.nv2a()->immediate_quad_fullscreen_texture_samples(),
                            mach.nv2a()->last_immediate_quad_fullscreen_raster_address(),
                            (unsigned long long)mach.nv2a()->immediate_quad_fullscreen_raster_output_hash(),
                            (unsigned long long)mach.nv2a()->immediate_quad_fullscreen_candidates(),
                            mach.nv2a()->last_immediate_quad_fullscreen_reject_mask(),
                            mach.nv2a()->last_immediate_quad_fullscreen_raster_reject_mask());
                std::printf("[2bl] NV2A vertices: batches=%llu submitted=%llu "
                            "attr-fetches=%llu primitives=%llu triangles=%llu max-index=%u\n",
                            (unsigned long long)mach.nv2a()->vertex_batches(),
                            (unsigned long long)mach.nv2a()->submitted_vertices(),
                            (unsigned long long)mach.nv2a()->vertex_attribute_fetches(),
                            (unsigned long long)mach.nv2a()->assembled_primitives(),
                            (unsigned long long)mach.nv2a()->assembled_triangles(),
                            mach.nv2a()->largest_vertex_index());
                std::printf("[2bl] NV2A immediate vertices: methods=%llu vertices=%llu "
                            "batches=%llu\n",
                            (unsigned long long)mach.nv2a()->immediate_methods(),
                            (unsigned long long)mach.nv2a()->immediate_vertices(),
                            (unsigned long long)mach.nv2a()->immediate_batches());
                std::printf("[2bl] NV2A inline arrays: batches/words/vertices/"
                            "attr-decodes=%llu/%llu/%llu/%llu\n",
                            (unsigned long long)mach.nv2a()->inline_array_batches(),
                            (unsigned long long)mach.nv2a()->inline_array_words(),
                            (unsigned long long)mach.nv2a()->inline_array_vertices(),
                            (unsigned long long)mach.nv2a()->inline_attribute_decodes());
                std::printf("[2bl] NV2A vertex modes:");
                for (uint32_t mode = 1; mode <= 10; ++mode) {
                    uint64_t count = mach.nv2a()->primitive_mode_count(mode);
                    if (count)
                        std::printf(" %u=%llu", mode, (unsigned long long)count);
                }
                std::printf(" formats:");
                for (uint32_t format = 0; format <= 6; ++format) {
                    uint64_t count = mach.nv2a()->vertex_format_fetches(format);
                    if (count)
                        std::printf(" %u=%llu", format, (unsigned long long)count);
                }
                std::printf(" slots:");
                for (uint32_t slot = 0; slot < 16; ++slot) {
                    uint64_t count = mach.nv2a()->vertex_slot_fetches(slot);
                    if (count)
                        std::printf(" %u=%llu", slot, (unsigned long long)count);
                }
                std::printf("\n");
                std::printf("[2bl] NV2A transform: fixed/prog=%llu/%llu "
                            "vertices=%llu program/constant-words=%llu/%llu "
                            "position-fnv64=%016llx\n",
                            (unsigned long long)mach.nv2a()->transform_mode_batches(0),
                            (unsigned long long)mach.nv2a()->transform_mode_batches(2),
                            (unsigned long long)mach.nv2a()->transformed_vertices(),
                            (unsigned long long)mach.nv2a()->transform_program_words(),
                            (unsigned long long)mach.nv2a()->transform_constant_words(),
                            (unsigned long long)mach.nv2a()->transform_output_hash());
                std::printf("[2bl] NV2A fixed-function: skin");
                for (uint32_t mode = 0; mode <= 6; ++mode) {
                    uint64_t count = mach.nv2a()->fixed_skin_batches(mode);
                    if (count)
                        std::printf(" %u=%llu", mode, (unsigned long long)count);
                }
                std::printf(" lighting/norm/spec/fog/texmat/texgen=%llu/%llu/%llu/%llu/%llu/%llu\n",
                            (unsigned long long)mach.nv2a()->fixed_lighting_batches(),
                            (unsigned long long)mach.nv2a()->fixed_normalization_batches(),
                            (unsigned long long)mach.nv2a()->fixed_specular_batches(),
                            (unsigned long long)mach.nv2a()->fixed_fog_batches(),
                            (unsigned long long)mach.nv2a()->fixed_texture_matrix_batches(),
                            (unsigned long long)mach.nv2a()->fixed_texgen_batches());
                std::printf("[2bl] NV2A fixed lights off/inf/local/spot=%llu/%llu/%llu/%llu",
                            (unsigned long long)mach.nv2a()->fixed_light_type_batches(0),
                            (unsigned long long)mach.nv2a()->fixed_light_type_batches(1),
                            (unsigned long long)mach.nv2a()->fixed_light_type_batches(2),
                            (unsigned long long)mach.nv2a()->fixed_light_type_batches(3));
                std::printf(" material=");
                bool first_material = true;
                for (uint32_t sources = 0; sources < 256; ++sources) {
                    uint64_t count =
                        mach.nv2a()->fixed_color_material_batches(sources);
                    if (count) {
                        std::printf("%s%02X:%llu", first_material ? "" : ",",
                                    sources, (unsigned long long)count);
                        first_material = false;
                    }
                }
                std::printf(" lightctl=");
                bool first_light_control = true;
                for (uint32_t flags = 0; flags < 8; ++flags) {
                    uint64_t count =
                        mach.nv2a()->fixed_light_control_batches(flags);
                    if (count) {
                        std::printf("%s%u:%llu",
                                    first_light_control ? "" : ",",
                                    flags, (unsigned long long)count);
                        first_light_control = false;
                    }
                }
                std::printf("\n");
                std::vector<Nv2a::RasterStateSample> raster_samples;
                mach.nv2a()->raster_state_samples(raster_samples);
                std::sort(raster_samples.begin(), raster_samples.end(),
                          [](const auto& a, const auto& b) {
                              const uint64_t a_skipped =
                                  a.batches - a.executed_batches;
                              const uint64_t b_skipped =
                                  b.batches - b.executed_batches;
                              return a_skipped != b_skipped
                                  ? a_skipped > b_skipped
                                  : a.batches > b.batches;
                          });
                size_t skipped_states = 0;
                uint64_t skipped_batches = 0;
                for (const auto& sample : raster_samples) {
                    const uint64_t skipped =
                        sample.batches - sample.executed_batches;
                    if (skipped) ++skipped_states;
                    skipped_batches += skipped;
                }
                std::printf("[2bl] NV2A raster states: unique=%zu "
                            "recorder-only states/batches=%zu/%llu\n",
                            raster_samples.size(), skipped_states,
                            (unsigned long long)skipped_batches);
                auto print_immediate_quad_evidence =
                    [](const Nv2a::RasterStateSample& sample) {
                    if (!sample.immediate_quad_batches) return;
                    const bool literal_vsh =
                        sample.immediate_quad_transform_mode == 2;
                    const bool single_identity =
                        sample.immediate_quad_transform_variant_count == 1 &&
                        sample.immediate_quad_later_transform_batches == 0 &&
                        sample.immediate_quad_transform_variant_overflow_batches == 0;
                    std::printf(
                        "[2bl]     immediate-quad batches=%llu vertices=%u..%u non4=%llu "
                        "first-xf=%u/%u first-vsh-fnv-all=%016llx "
                        "first-constants-fnv-all=%016llx identities=%s "
                        "transform-variant-count(stored)=%u cap=%zu "
                        "first-batches=%llu later-variant-batches=%llu overflow-new=%llu\n",
                        (unsigned long long)sample.immediate_quad_batches,
                        sample.immediate_quad_min_vertices,
                        sample.immediate_quad_max_vertices,
                        (unsigned long long)sample.immediate_quad_nonmultiple_of_four,
                        sample.immediate_quad_transform_mode,
                        sample.immediate_quad_transform_start,
                        (unsigned long long)sample.immediate_quad_program_hash,
                        (unsigned long long)sample.immediate_quad_constants_hash,
                        single_identity ? "single" : "varied",
                        sample.immediate_quad_transform_variant_count,
                        Nv2a::RasterStateSample::kImmediateQuadTransformVariants,
                        (unsigned long long)
                            sample.immediate_quad_first_transform_batches,
                        (unsigned long long)
                            sample.immediate_quad_later_transform_batches,
                        (unsigned long long)
                            sample.immediate_quad_transform_variant_overflow_batches);
                    if (!sample.immediate_quad_first_captured) return;
                    auto print_float4 = [](const char* label,
                                           const std::array<float, 4>& value) {
                        uint32_t bits[4]{};
                        std::memcpy(bits, value.data(), sizeof(bits));
                        std::printf(" %s=%g[%08x]/%g[%08x]/%g[%08x]/%g[%08x]",
                                    label, value[0], bits[0], value[1], bits[1],
                                    value[2], bits[2], value[3], bits[3]);
                    };
                    for (size_t vertex = 0; vertex < 4; ++vertex) {
                        std::printf("[2bl]       IQ%zu", vertex);
                        print_float4("decoded-input0",
                                     sample.immediate_quad_decoded_input0[vertex]);
                        print_float4("decoded-input3",
                                     sample.immediate_quad_decoded_input3[vertex]);
                        print_float4("decoded-input9",
                                     sample.immediate_quad_decoded_input9[vertex]);
                        print_float4(literal_vsh ? "literal-oPos" : "derived-oPos",
                                     sample.immediate_quad_opos[vertex]);
                        print_float4(literal_vsh ? "literal-oD0" : "derived-oD0",
                                     sample.immediate_quad_od0[vertex]);
                        print_float4(literal_vsh ? "literal-oT0" : "derived-oT0",
                                     sample.immediate_quad_ot0[vertex]);
                        print_float4("raster-oPos",
                                     sample.immediate_quad_raster_opos[vertex]);
                        std::printf("\n");
                    }
                    std::printf(
                        "[2bl]       IQ-T0 first-base-probe resolved=%u addr=%08x bytes=%u "
                        "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
                        sample.immediate_quad_t0_dma_resolved,
                        sample.immediate_quad_t0_address,
                        sample.immediate_quad_t0_probe_size,
                        sample.immediate_quad_t0_probe[0], sample.immediate_quad_t0_probe[1],
                        sample.immediate_quad_t0_probe[2], sample.immediate_quad_t0_probe[3],
                        sample.immediate_quad_t0_probe[4], sample.immediate_quad_t0_probe[5],
                        sample.immediate_quad_t0_probe[6], sample.immediate_quad_t0_probe[7],
                        sample.immediate_quad_t0_probe[8], sample.immediate_quad_t0_probe[9],
                        sample.immediate_quad_t0_probe[10], sample.immediate_quad_t0_probe[11],
                        sample.immediate_quad_t0_probe[12], sample.immediate_quad_t0_probe[13],
                        sample.immediate_quad_t0_probe[14], sample.immediate_quad_t0_probe[15]);
                    if (sample.immediate_quad_palette_relevant) {
                        std::printf(
                            "[2bl]       IQ-pal first-base-probe resolved=%u addr=%08x entries=%u "
                            "%08x/%08x/%08x/%08x\n",
                            sample.immediate_quad_palette_dma_resolved,
                            sample.immediate_quad_palette_address,
                            sample.immediate_quad_palette_probe_entries,
                            sample.immediate_quad_palette_probe[0],
                            sample.immediate_quad_palette_probe[1],
                            sample.immediate_quad_palette_probe[2],
                            sample.immediate_quad_palette_probe[3]);
                    }
                };
                const size_t raster_sample_limit =
                    std::min<size_t>(skipped_states, 16);
                for (size_t sample_index = 0;
                     sample_index < raster_sample_limit; ++sample_index) {
                    const auto& sample = raster_samples[sample_index];
                    const auto& state = sample.words;
                    std::printf(
                        "[2bl]   RS%zu batches/exec/skip=%llu/%llu/%llu "
                        "en=%04x sfmt=%08x pitch=%08x surf=%08x/%08x "
                        "clip=%08x/%08x win=%08x/%08x alpha=%x/%02x "
                        "blend=%x/%x/%x depth=%x/%u masks=%08x/%08x "
                        "cull=%x/%x tex=%x shader=%08x/%08x comb=%08x\n",
                        sample_index,
                        (unsigned long long)sample.batches,
                        (unsigned long long)sample.executed_batches,
                        (unsigned long long)(
                            sample.batches - sample.executed_batches),
                        state[Nv2a::kRasterEnables],
                        state[Nv2a::kRasterSurfaceFormat],
                        state[Nv2a::kRasterSurfacePitch],
                        state[Nv2a::kRasterSurfaceColorOffset],
                        state[Nv2a::kRasterSurfaceZetaOffset],
                        state[Nv2a::kRasterSurfaceClipHorizontal],
                        state[Nv2a::kRasterSurfaceClipVertical],
                        state[Nv2a::kRasterWindowClipHorizontal],
                        state[Nv2a::kRasterWindowClipVertical],
                        state[Nv2a::kRasterAlphaFunc],
                        state[Nv2a::kRasterAlphaRef] & 0xFFu,
                        state[Nv2a::kRasterBlendSrc],
                        state[Nv2a::kRasterBlendDst],
                        state[Nv2a::kRasterBlendEquation],
                        state[Nv2a::kRasterDepthFunc],
                        state[Nv2a::kRasterDepthMask],
                        state[Nv2a::kRasterColorMask],
                        state[Nv2a::kRasterStencilWriteMask],
                        state[Nv2a::kRasterCullFace],
                        state[Nv2a::kRasterFrontFace],
                        state[Nv2a::kRasterTextureEnableMask],
                        state[Nv2a::kRasterShaderStageProgram],
                        state[Nv2a::kRasterShaderOtherStageInput],
                        state[Nv2a::kRasterCombinerControl]);
                    std::printf("[2bl]     geom vertices=%llu inline=%llu prim=",
                                (unsigned long long)sample.vertices,
                                (unsigned long long)sample.inline_batches);
                    bool first_primitive = true;
                    for (uint32_t mode = 1;
                         mode < sample.primitive_batches.size(); ++mode) {
                        if (sample.primitive_batches[mode]) {
                            std::printf("%s%u:%llu",
                                        first_primitive ? "" : ",", mode,
                                        (unsigned long long)
                                            sample.primitive_batches[mode]);
                            first_primitive = false;
                        }
                    }
                    std::printf("\n");
                    std::printf(
                        "[2bl]     ctrl=%08x shade=%x poly=%x/%x "
                        "poffset=%08x/%08x zclip=%08x/%08x zctl=%08x shadow=%x "
                        "stencil=%x/%02x/%08x ops=%x/%x/%x\n",
                        state[Nv2a::kRasterControl0],
                        state[Nv2a::kRasterShadeMode],
                        state[Nv2a::kRasterFrontPolygonMode],
                        state[Nv2a::kRasterBackPolygonMode],
                        state[Nv2a::kRasterPolygonOffsetScale],
                        state[Nv2a::kRasterPolygonOffsetBias],
                        state[Nv2a::kRasterClipMin],
                        state[Nv2a::kRasterClipMax],
                        state[Nv2a::kRasterZMinMaxControl],
                        state[Nv2a::kRasterShadowDepthFunc],
                        state[Nv2a::kRasterStencilFunc],
                        state[Nv2a::kRasterStencilRef] & 0xFFu,
                        state[Nv2a::kRasterStencilFuncMask],
                        state[Nv2a::kRasterStencilOpFail],
                        state[Nv2a::kRasterStencilOpZFail],
                        state[Nv2a::kRasterStencilOpZPass]);
                    const uint32_t combiner_stages =
                        std::min<uint32_t>(
                            state[Nv2a::kRasterCombinerControl] & 0xFFu, 8);
                    for (uint32_t stage = 0; stage < combiner_stages; ++stage) {
                        std::printf(
                            "[2bl]     C%u ci=%08x ai=%08x co=%08x ao=%08x "
                            "f=%08x/%08x\n",
                            stage,
                            state[Nv2a::kRasterCombinerColorIcw0 + stage],
                            state[Nv2a::kRasterCombinerAlphaIcw0 + stage],
                            state[Nv2a::kRasterCombinerColorOcw0 + stage],
                            state[Nv2a::kRasterCombinerAlphaOcw0 + stage],
                            state[Nv2a::kRasterCombinerFactor0_0 + stage],
                            state[Nv2a::kRasterCombinerFactor1_0 + stage]);
                    }
                    std::printf("[2bl]     final=%08x/%08x factors=%08x/%08x\n",
                                state[Nv2a::kRasterCombinerSpecularFog0],
                                state[Nv2a::kRasterCombinerSpecularFog1],
                                state[Nv2a::kRasterCombinerFinalFactor0],
                                state[Nv2a::kRasterCombinerFinalFactor1]);
                    if (state[Nv2a::kRasterTextureEnableMask]) {
                        std::printf("[2bl]     texture-dma=%08x/%08x\n",
                                    state[Nv2a::kRasterTextureDmaA],
                                    state[Nv2a::kRasterTextureDmaB]);
                    }
                    for (uint32_t stage = 0; stage < 4; ++stage) {
                        if (!(state[Nv2a::kRasterTextureEnableMask] &
                              (1u << stage)))
                            continue;
                        std::printf(
                            "[2bl]     T%u off=%08x fmt=%08x addr=%08x "
                            "filter=%08x ctl0=%08x ctl1=%08x rect=%08x "
                            "pal=%08x border=%08x\n",
                            stage,
                            state[Nv2a::kRasterTextureOffset0 + stage],
                            state[Nv2a::kRasterTextureFormat0 + stage],
                            state[Nv2a::kRasterTextureAddress0 + stage],
                            state[Nv2a::kRasterTextureFilter0 + stage],
                            state[Nv2a::kRasterTextureControl0_0 + stage],
                            state[Nv2a::kRasterTextureControl1_0 + stage],
                            state[Nv2a::kRasterTextureImageRect0 + stage],
                            state[Nv2a::kRasterTexturePalette0 + stage],
                            state[Nv2a::kRasterTextureBorderColor0 + stage]);
                    }
                }
                size_t immediate_state_index = 0;
                for (const auto& sample : raster_samples) {
                    if (!sample.immediate_quad_batches) continue;
                    const auto& state = sample.words;
                    const size_t iqrs_index = immediate_state_index++;
                    std::printf(
                        "[2bl]   IQRS%zu (all mode8 states) batches=%llu sfmt=%08x "
                        "pitch=%08x surf=%08x/%08x clip=%08x/%08x T0=%08x/%08x "
                        "dma=%08x/%08x\n",
                        iqrs_index,
                        (unsigned long long)sample.immediate_quad_batches,
                        state[Nv2a::kRasterSurfaceFormat],
                        state[Nv2a::kRasterSurfacePitch],
                        state[Nv2a::kRasterSurfaceColorOffset],
                        state[Nv2a::kRasterSurfaceZetaOffset],
                        state[Nv2a::kRasterSurfaceClipHorizontal],
                        state[Nv2a::kRasterSurfaceClipVertical],
                        state[Nv2a::kRasterTextureOffset0],
                        state[Nv2a::kRasterTextureFormat0],
                        state[Nv2a::kRasterTextureDmaA],
                        state[Nv2a::kRasterTextureDmaB]);
                    // The capped RS listing above is useful for high-volume
                    // recorder-only states, but it can omit an immediate
                    // family whose exact pixel tuple is still unknown. Keep
                    // every retained word here, keyed to the same IQRS index,
                    // so a later profile can be grounded without changing
                    // emulation or guessing omitted state.
                    for (size_t word = 0; word < state.size(); word += 8) {
                        std::printf("[2bl]     IQRS%zu state[%03zu..%03zu]=",
                                    iqrs_index, word,
                                    std::min(word + 7, state.size() - 1));
                        for (size_t column = word;
                             column < std::min(word + 8, state.size()); ++column) {
                            std::printf("%s%08x", column == word ? "" : "/",
                                        state[column]);
                        }
                        std::printf("\n");
                    }
                    print_immediate_quad_evidence(sample);
                }
                std::printf("[2bl] NV2A flip: write-increments=%llu stalls=%llu "
                            "read-increments=%llu notifications=%llu surface=%08x waiting=%u\n",
                            (unsigned long long)mach.nv2a()->flip_increments(),
                            (unsigned long long)mach.nv2a()->flip_stalls(),
                            (unsigned long long)mach.nv2a()->pgraph_read_increments(),
                            (unsigned long long)mach.nv2a()->pgraph_notifications(),
                            mach.nv2a()->pgraph_surface(), mach.nv2a()->waiting_for_flip());
                if (frame_available) {
                    std::printf("[2bl] NV2A scanout: start=%08x pitch=%u size=%ux%u bpp=%u "
                                "writes/nonzero=%llu/%llu last-nonzero=%08x "
                                "rgba-fnv64=%016llx\n",
                                scanout.start, scanout.pitch, scanout.width, scanout.height,
                                scanout.bits_per_pixel,
                                (unsigned long long)mach.nv2a()->scanout_writes(),
                                (unsigned long long)mach.nv2a()->scanout_nonzero_writes(),
                                mach.nv2a()->last_nonzero_scanout_start(),
                                (unsigned long long)frame_hash(frame_rgba));
                } else {
                    std::printf("[2bl] NV2A scanout unavailable: %s\n",
                                scanout_error.c_str());
                }
                std::vector<uint32_t> scanout_starts;
                mach.nv2a()->recent_scanout_starts(16, scanout_starts);
                std::printf("[2bl] NV2A recent PCRTC_START:");
                for (uint32_t start : scanout_starts) std::printf(" %08x", start);
                std::printf("\n");
                if (pfifo_clear_trace) {
                    std::vector<Nv2a::PushMethod> trace;
                    mach.nv2a()->color_clear_trace(trace);
                    std::printf("[2bl] PFIFO clear trace: target=%08x clear-triggered=%u "
                                "begin-triggered=%u complete=%u truncated=%u "
                                "clear-seq=%llu begin-seq=%llu pre=%zu captured=%zu\n",
                                mach.nv2a()->color_clear_trace_target(),
                                mach.nv2a()->color_clear_trace_triggered(),
                                mach.nv2a()->color_clear_trace_begin_triggered(),
                                mach.nv2a()->color_clear_trace_complete(),
                                mach.nv2a()->color_clear_trace_truncated(),
                                (unsigned long long)mach.nv2a()->color_clear_trace_trigger_seq(),
                                (unsigned long long)mach.nv2a()->color_clear_trace_begin_seq(),
                                mach.nv2a()->color_clear_trace_pre_count(), trace.size());
                    for (const auto& m : trace) {
                        std::printf("[2bl]   clear-trace PB #%llu ch%u/sub%u class=0x%02x "
                                    "get=%08x put=%08x method=0x%04x param=0x%08x\n",
                                    (unsigned long long)m.seq, m.channel, m.subchannel,
                                    m.object_class, m.parser_get, m.parser_put,
                                    m.method, m.parameter);
                    }
                }
                std::vector<Nv2a::PushMethod> recent;
                mach.nv2a()->recent_methods(64, recent);
                for (const auto& m : recent) {
                    std::printf("[2bl]   PB #%llu ch%u/sub%u class=0x%02x get=%08x put=%08x "
                                "method=0x%04x param=0x%08x\n",
                                (unsigned long long)m.seq, m.channel, m.subchannel,
                                m.object_class, m.parser_get, m.parser_put, m.method,
                                m.parameter);
                }
                uint32_t av_sync = 0;
                if (mach.read(0x03FB3000, &av_sync, sizeof(av_sync)))
                    std::printf("[2bl] AV sync RAM [0x03fb3000]=0x%08x\n", av_sync);
            }
            if (mach.apu()) {
                std::printf("[2bl] APU: VOICE_OFF methods=%llu idle-voice traps=%llu\n",
                            (unsigned long long)mach.apu()->voice_off_methods(),
                            (unsigned long long)mach.apu()->idle_voice_traps());
                std::printf("[2bl] APU regs: ISTS=%08x IEN=%08x FECTL=%08x FECV=%08x "
                            "METH=%08x PARAM=%08x FORCE1=%08x SECTL=%08x VPV=%08x "
                            "TOP=%04x/%04x/%04x CUR=%04x/%04x/%04x\n",
                            mach.apu()->mmio_read(McpxApu::kBase + 0x1000, 4),
                            mach.apu()->mmio_read(McpxApu::kBase + 0x1004, 4),
                            mach.apu()->mmio_read(McpxApu::kBase + 0x1100, 4),
                            mach.apu()->mmio_read(McpxApu::kBase + 0x1110, 4),
                            mach.apu()->mmio_read(McpxApu::kBase + 0x1300, 4),
                            mach.apu()->mmio_read(McpxApu::kBase + 0x1304, 4),
                            mach.apu()->mmio_read(McpxApu::kBase + 0x1504, 4),
                            mach.apu()->mmio_read(McpxApu::kBase + 0x2000, 4),
                            mach.apu()->mmio_read(McpxApu::kBase + 0x202c, 4),
                            (unsigned)mach.apu()->mmio_read(McpxApu::kBase + 0x2054, 4),
                            (unsigned)mach.apu()->mmio_read(McpxApu::kBase + 0x2060, 4),
                            (unsigned)mach.apu()->mmio_read(McpxApu::kBase + 0x206c, 4),
                            (unsigned)mach.apu()->mmio_read(McpxApu::kBase + 0x2058, 4),
                            (unsigned)mach.apu()->mmio_read(McpxApu::kBase + 0x2064, 4),
                            (unsigned)mach.apu()->mmio_read(McpxApu::kBase + 0x2070, 4));
            }
            if (mach.aci()) {
                std::printf("[2bl] ACI: completed=%llu GLOB_STA=%08x "
                            "PCM CIV/LVI/SR=%08x PICB/PIV/CR=%08x\n",
                            (unsigned long long)mach.aci()->completed_descriptors(),
                            mach.aci()->mmio_read(McpxAci::kBase + 0x130, 4),
                            mach.aci()->mmio_read(McpxAci::kBase + 0x114, 4),
                            mach.aci()->mmio_read(McpxAci::kBase + 0x118, 4));
            }
            if (mach.ohci0()) {
                const uint32_t ohci_base = mach.ohci0()->mmio_base();
                std::array<uint32_t, 32> periodic_heads{};
                uint32_t periodic_nonzero = 0;
                if (mach.ohci0()->hcca() &&
                    mach.read(mach.ohci0()->hcca(), periodic_heads.data(),
                              periodic_heads.size() * sizeof(uint32_t))) {
                    for (uint32_t head : periodic_heads)
                        periodic_nonzero += (head & ~0xFu) != 0;
                }
                std::printf("[2bl] OHCI0: frames=%llu control=%08x hcca=%08x "
                            "eds/tds/retired/done=%llu/%llu/%llu/%llu "
                            "control/xid-in/xid-endpoint2/xid-out=%llu/%llu/%llu/%llu "
                            "addr/config=%u/%u hub-addr/config/port1=%u/%u/%08x "
                            "rumble=%04x/%04x\n",
                            (unsigned long long)mach.ohci0()->frames(),
                            mach.ohci0()->control(), mach.ohci0()->hcca(),
                            (unsigned long long)mach.ohci0()->processed_eds(),
                            (unsigned long long)mach.ohci0()->processed_tds(),
                            (unsigned long long)mach.ohci0()->retired_tds(),
                            (unsigned long long)mach.ohci0()->done_writebacks(),
                            (unsigned long long)mach.ohci0()->control_requests(),
                            (unsigned long long)mach.ohci0()->xid_input_reports(),
                            (unsigned long long)mach.ohci0()->xid_endpoint2_input_reports(),
                            (unsigned long long)mach.ohci0()->xid_output_reports(),
                            mach.ohci0()->xid_address(),
                            mach.ohci0()->xid_configuration(),
                            mach.ohci0()->synthetic_hub_address(),
                            mach.ohci0()->synthetic_hub_configuration(),
                            mach.ohci0()->synthetic_hub_port_status(0),
                            mach.ohci0()->xid_left_rumble(),
                            mach.ohci0()->xid_right_rumble());
                std::printf(
                    "[2bl] OHCI0 lists: command=%08x periodic-current=%08x "
                    "control-head/current=%08x/%08x bulk-head/current=%08x/%08x "
                    "periodic-slots=%u\n",
                    mach.ohci0()->mmio_read(ohci_base + 0x08, 4),
                    mach.ohci0()->mmio_read(ohci_base + 0x1C, 4),
                    mach.ohci0()->mmio_read(ohci_base + 0x20, 4),
                    mach.ohci0()->mmio_read(ohci_base + 0x24, 4),
                    mach.ohci0()->mmio_read(ohci_base + 0x28, 4),
                    mach.ohci0()->mmio_read(ohci_base + 0x2C, 4),
                    periodic_nonzero);
                std::printf(
                    "[2bl] OHCI0 irq: status/enable=%08x/%08x done-head=%08x "
                    "wdh-assert/w1c=%llu/%llu port0=%08x ep0-trace=%zu dropped=%llu\n",
                    mach.ohci0()->interrupt_status(),
                    mach.ohci0()->interrupt_enable(),
                    mach.ohci0()->last_done_head(),
                    (unsigned long long)mach.ohci0()->wdh_assertions(),
                    (unsigned long long)mach.ohci0()->wdh_w1c_acknowledgements(),
                    mach.ohci0()->port_status(0),
                    mach.ohci0()->ep0_trace_count(),
                    (unsigned long long)mach.ohci0()->ep0_trace_dropped());
                auto ep0_outcome_name = [](Ohci::Ep0Outcome outcome) {
                    switch (outcome) {
                        case Ohci::Ep0Outcome::kPending: return "pending";
                        case Ohci::Ep0Outcome::kComplete: return "complete";
                        case Ohci::Ep0Outcome::kStalled: return "stall";
                        case Ohci::Ep0Outcome::kError: return "error";
                    }
                    return "unknown";
                };
                for (size_t i = 0; i < mach.ohci0()->ep0_trace_count(); ++i) {
                    const auto& trace = mach.ohci0()->ep0_trace(i);
                    std::printf(
                        "[2bl]   OHCI0 EP0[%zu] addr=%u setup=%02x/%02x "
                        "value=%04x index=%04x len=%u outcome=%s td=%u cc=%u bytes=%u "
                        "terminal=%08x flags=%08x R=%u ed-head=%08x\n",
                        i, trace.address, trace.request_type, trace.request,
                        trace.value, trace.index, trace.length,
                        ep0_outcome_name(trace.outcome), trace.td_completions,
                        trace.last_condition_code, trace.data_bytes,
                        trace.terminal_td_address, trace.terminal_td_flags,
                        (trace.terminal_td_flags >> 18) & 1u,
                        trace.ed_head_after);
                }
            }
            if (mach.nvnet()) {
                std::printf("[2bl] NVNet: link=%s MDIO rd/wr=%llu/%llu "
                            "TX kick/desc/pkt/bytes=%llu/%llu/%llu/%llu "
                            "RX offer/desc/pkt/bytes=%llu/%llu/%llu/%llu "
                            "drop filter/oversize/not-ready=%llu/%llu/%llu "
                            "IRQ status/mask=%08x/%08x TXRX/RXCTL=%08x/%08x "
                            "RX current/next=%08x/%08x\n",
                            mach.nvnet()->link_up() ? "up" : "down",
                            (unsigned long long)mach.nvnet()->mdio_reads(),
                            (unsigned long long)mach.nvnet()->mdio_writes(),
                            (unsigned long long)mach.nvnet()->tx_kicks(),
                            (unsigned long long)mach.nvnet()->tx_descriptors(),
                            (unsigned long long)mach.nvnet()->tx_packets(),
                            (unsigned long long)mach.nvnet()->tx_bytes(),
                            (unsigned long long)mach.nvnet()->rx_offers(),
                            (unsigned long long)mach.nvnet()->rx_descriptors(),
                            (unsigned long long)mach.nvnet()->rx_packets(),
                            (unsigned long long)mach.nvnet()->rx_bytes(),
                            (unsigned long long)mach.nvnet()->rx_filtered(),
                            (unsigned long long)mach.nvnet()->rx_oversized(),
                            (unsigned long long)mach.nvnet()->rx_not_ready(),
                            mach.nvnet()->irq_status(), mach.nvnet()->irq_mask(),
                            mach.nvnet()->tx_rx_control(),
                            mach.nvnet()->receiver_control(),
                            mach.nvnet()->rx_current_descriptor(),
                            mach.nvnet()->rx_next_descriptor());
                if (net_transport) {
                    std::printf("[2bl] NVNet UDP: tx pkt/bytes=%llu/%llu "
                                "rx pkt/bytes=%llu/%llu\n",
                                (unsigned long long)net_transport->tx_packets(),
                                (unsigned long long)net_transport->tx_bytes(),
                                (unsigned long long)net_transport->rx_packets(),
                                (unsigned long long)net_transport->rx_bytes());
                }
            }
            std::printf("[2bl] IDE: port accesses=%llu irqs=%llu sectors rd/wr=%llu/%llu "
                        "bm_base=0x%04x\n", (unsigned long long)ide_io_count,
                        (unsigned long long)(mach.ide() ? mach.ide()->irq_pulses() : 0),
                        (unsigned long long)(mach.ide() ? mach.ide()->sectors_read() : 0),
                        (unsigned long long)(mach.ide() ? mach.ide()->sectors_written() : 0),
                        mach.ide() ? mach.ide()->bm_base() : 0);
            std::printf("[2bl] MCPX ROM overlay disabled by firmware: %s\n",
                        mach.pci() && mach.pci()->mcpx_rom_disabled() ? "yes" : "no");
            std::printf("[2bl] IRQ0 pulses=%llu, pending-boundaries=%llu, delivered=%llu\n",
                        (unsigned long long)mach.irq0_pulses(),
                        (unsigned long long)mach.pending_boundaries(),
                        (unsigned long long)mach.injected_interrupts());
            if (mach.pic_dbg()) {
                auto& m = mach.pic_dbg()->master();
                auto& s = mach.pic_dbg()->slave();
                std::printf("[2bl] PIC master: imr=0x%02x irr=0x%02x isr=0x%02x base=0x%02x "
                            "init=%u | slave: imr=0x%02x irr=0x%02x base=0x%02x\n",
                            m.imr, m.irr, m.isr, m.irq_base, m.init_state, s.imr, s.irr,
                            s.irq_base);
            }
            std::printf("[2bl] last %zu I/O ops:\n", io_log.size());
            for (const auto& e : io_log)
                std::printf("      %-3s port=0x%04x size=%u value=0x%08x\n", e.out ? "OUT" : "IN",
                            e.port, e.size, e.value);
            std::printf("[2bl] last %zu MMIO ops:\n", mmio_log.size());
            for (const auto& e : mmio_log)
                std::printf("      %-3s addr=0x%08x size=%u value=0x%08x\n",
                            e.write ? "WR" : "RD", e.addr, e.size, e.value);
            if (frame_dump) {
                frame_output_ok =
                    frame_available &&
                    write_frame_ppm(frame_dump, frame_rgba, scanout.width, scanout.height);
                std::printf("[host] frame dump %s: %s\n", frame_dump,
                            frame_output_ok ? "written" : "FAILED");
            }
            if (debug_surface_dump) {
                std::vector<uint8_t> surface_rgba;
                std::string surface_error;
                const bool surface_captured = capture_argb32_surface(
                    mach, debug_surface_address, debug_surface_pitch,
                    debug_surface_width, debug_surface_height, surface_rgba,
                    surface_error);
                const bool surface_written =
                    surface_captured &&
                    write_frame_ppm(debug_surface_dump, surface_rgba,
                                    debug_surface_width, debug_surface_height);
                frame_output_ok = frame_output_ok && surface_written;
                std::printf("[host] debug surface dump %s addr=%08x pitch=%u "
                            "size=%ux%u hash=%016llx: %s%s%s\n",
                            debug_surface_dump, debug_surface_address,
                            debug_surface_pitch, debug_surface_width,
                            debug_surface_height,
                            static_cast<unsigned long long>(
                                surface_captured ? frame_hash(surface_rgba) : 0),
                            surface_written ? "written" : "FAILED",
                            surface_error.empty() ? "" : " (",
                            surface_error.empty() ? "" : (surface_error + ")").c_str());
            }
            if (window && frame_available)
                run_frame_window("xlle NV2A scanout", static_cast<int>(scanout.width),
                                 static_cast<int>(scanout.height), frame_rgba.data(),
                                 static_cast<size_t>(scanout.width) * 4);
            if (!err2.empty()) return 2;
            return frame_output_ok ? 0 : 3;
        }
        std::printf("[machine] harness OK. NEXT: MCPX X-code repro + 2BL RC4-decrypt to boot.\n");
        return 0;
    }

    DebugServer dbg;
    dbg.register_ring("retired", dump_retired);
    dbg.register_ring("mem", dump_mem);
    if (!dbg.start(4370))
        std::fprintf(stderr, "[obs] debug server: %s\n", dbg.last_error().c_str());
    else
        std::printf("[obs] debug server on tcp/%u (ping | rings | query <ring> <n>)\n",
                    dbg.port());

    std::printf("[spike] CPU floor = unicorn (x86-32); %zu fixtures\n", corpus().size());
    int pass = 0, fail = 0;
    for (const auto& f : corpus()) (run_fixture(f) ? pass : fail)++;
    std::printf("[spike] %d passed, %d failed; rings: retired=%llu mem=%llu\n", pass, fail,
                (unsigned long long)g_retired.total(), (unsigned long long)g_mem.total());

    if (window) run_window("xlle A0 host", 640, 480);

    if (serve) {
        std::printf("[obs] serving; connect to tcp/4370 (Ctrl-C to exit)\n");
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(3600));
    }
    dbg.stop();
    return fail == 0 ? 0 : 1;
}
