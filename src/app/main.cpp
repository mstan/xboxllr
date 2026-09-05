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
#include "cpu/unicorn_metrics.h"
#include "host/ethernet_udp.hpp"
#include "host/nv2a_opengl_renderer.hpp"
#include "host/sample_profiler.hpp"

// Heap poisoning (src/host/heap_poison.cpp). Selected by XLLE_HEAP_POISON, not by argv,
// because allocations start before argv is parsed.
bool xlle_heap_poison_enabled();
int xlle_heap_poison_byte();
#include "host/sdl_shell.hpp"
#include "host/xid_script.hpp"
#include "machine/aci.hpp"
#include "machine/apu.hpp"
#include "machine/machine.hpp"
#include "machine/perf_accounting.hpp"
#include "machine/perf_excl.hpp"
#include "obs/debug_server.hpp"
#include "spike/fixtures.hpp"
#include "xlle/jsonl.hpp"
#include "xlle/ring.hpp"

using namespace xlle;

namespace {

Ring<RetiredInsn> g_retired(1u << 16);
Ring<MemEvent> g_mem(1u << 16);
constexpr uint64_t kMinFrameSeriesInterval = 1000000;

// ============ stage (d) seam census printer (beads-xbox.1.15.4) =============
// DESIGN-NV2A-SEAM.md measurement M5. Stage (d) has no job queue: every DrawJob
// runs inline at the latch point. The ten coherence anchors of section 2.2(c)
// therefore do nothing but COUNT how often a threaded consumer would have had to
// stop and drain, and how deep the queue would have been when it did. That is
// what sizes -- or kills -- stage (e), and it comes free with stage (d).
//
// Printed at every frame-series checkpoint as an interval delta and once at
// shutdown as the cumulative total. Parsed additively by
// tools/measure_xbox_replay.py.
struct SeamCensusSnapshot {
    uint64_t jobs_latched = 0;
    uint64_t jobs_threadable = 0;
    uint64_t jobs_queueable = 0;
    uint64_t jobs_not_executed = 0;
    uint64_t vblanks = 0;
    uint64_t vblank_jobs_sum = 0;
    uint64_t resolve_transfers = 0;
    std::array<uint64_t, xlle::Nv2a::kSeamNotThreadableReasons> not_threadable{};
    std::array<uint64_t, xlle::Nv2a::kSeamAnchorCount> hits{};
    std::array<uint64_t, xlle::Nv2a::kSeamAnchorCount> drains{};
    std::array<uint64_t, xlle::Nv2a::kSeamAnchorCount> pending_sum{};
    // Stage (e) (beads-xbox.1.15.5): the real queue, not the simulation.
    uint64_t jobs_queued = 0;
    uint64_t jobs_inline = 0;
    uint64_t backpressure_inline = 0;
    uint64_t backpressure_bytes = 0;
    uint64_t order_fences = 0;
    uint64_t drains_empty = 0;
    uint64_t drains_nonempty = 0;
    uint64_t depth_sum = 0;
    uint64_t depth_samples = 0;
    uint64_t worker_busy_ticks = 0;
    uint64_t drain_wait_ticks = 0;
    uint64_t elapsed_ticks = 0;
    uint64_t worker_failures = 0;
    std::array<uint64_t, xlle::Nv2a::kDrawDepthBuckets> depth_hist{};
    // Stage (e2) (beads-xbox.1.15.9): the destination-span partition. span_jobs
    // is the witness that work division actually ran -- an A/B whose span_jobs
    // is 0 measured nothing, which is the trap ndsrecomp documented for its own
    // threaded_lines / inline_lines pair.
    uint64_t span_jobs = 0;
    uint64_t span_inline_jobs = 0;
    uint64_t span_barrier_wait_ticks = 0;
    uint64_t span_part0_busy_ticks = 0;
    uint64_t span_helper_busy_ticks = 0;
    // beads-xbox.1.15.9 loose end 2: the per-part telemetry merge, MEASURED
    // rather than estimated. Only accrues under --perf-accounting (the two clock
    // reads are gated on perf_excl::g_enabled), so it is 0 on every noperf leg
    // by design, not by omission.
    uint64_t span_fold_ticks = 0;
    // F3 closure (beads-xbox.1.15.5 residual risk): renderer residency queries
    // that found the ring non-empty and drained BEFORE asking. While this stays
    // 0 the query result provably could not have depended on a pending job.
    uint64_t renderer_query_fences = 0;
};

// Stage (f) GPU-residency census (beads-xbox.1.15.7). Cumulative-only: the
// state machine accumulates from process start, so the interval form would only
// re-derive differences the reader can take from two cumulative lines.
void print_gpu_residency_census(const xlle::Nv2a* nv, const char* scope,
                                unsigned long long deadline,
                                unsigned long long vblanks) {
    if (!nv) return;
    const auto& c = nv->gpu_residency_census();
    const double vb = vblanks ? (double)vblanks : 1.0;
    std::printf(
        "[host] gpu-residency scope=%s deadline=%llu gpu_draws=%llu "
        "cpu_draws=%llu uploads=%llu upload_mb=%.1f resolves=%llu "
        "resolve_mb=%.1f depth_uploads=%llu depth_upload_mb=%.1f "
        "depth_resolves=%llu depth_resolve_mb=%.1f clears_on_gpu=%llu "
        "runs=%llu run_mean=%.2f run_max=%llu "
        "resolve_mb_per_vblank=%.3f upload_mb_per_vblank=%.3f "
        "sem_resolves=%llu sem_resolve_mb=%.1f pgraph_read_resolves=%llu\n",
        scope, deadline,
        (unsigned long long)c.gpu_draws, (unsigned long long)c.cpu_draws,
        (unsigned long long)c.uploads, c.upload_bytes / 1048576.0,
        (unsigned long long)c.resolves, c.resolve_bytes / 1048576.0,
        (unsigned long long)c.depth_uploads, c.depth_upload_bytes / 1048576.0,
        (unsigned long long)c.depth_resolves, c.depth_resolve_bytes / 1048576.0,
        (unsigned long long)c.clears_on_gpu,
        (unsigned long long)c.runs,
        c.runs ? (double)c.run_sum / (double)c.runs : 0.0,
        (unsigned long long)c.run_max,
        (c.resolve_bytes + c.depth_resolve_bytes) / 1048576.0 / vb,
        (c.upload_bytes + c.depth_upload_bytes) / 1048576.0 / vb,
        (unsigned long long)c.semaphore_resolves,
        c.semaphore_resolve_bytes / 1048576.0,
        (unsigned long long)c.pgraph_read_resolves);
    std::printf(
        "[host] gpu-executor scope=%s deadline=%llu enabled=%u relaxed=%u mask=%u jobs=%llu "
        "triangles=%llu draw_calls=%llu declined=%llu surface_uploads=%llu "
        "surface_upload_mb=%.1f texture_upload_mb=%.1f\n",
        scope, deadline, nv->gpu_profiles_enabled() ? 1u : 0u,
        nv->gpu_exact_coords() ? 1u : 0u, nv->gpu_profile_mask(),
        (unsigned long long)nv->gpu_profile_jobs(),
        (unsigned long long)nv->gpu_profile_triangles(),
        (unsigned long long)nv->gpu_profile_draw_calls(),
        (unsigned long long)nv->gpu_profile_declined(),
        (unsigned long long)nv->gpu_profile_uploads(),
        nv->gpu_profile_upload_bytes() / 1048576.0,
        nv->gpu_profile_texture_upload_bytes() / 1048576.0);
    // beads-xbox.1.15.10 step 1 (overlap): the witness that the pending batch
    // is actually being issued early. bound is the configured triangle bound,
    // size/kick are the two early triggers and gl_flushes is how many draws
    // were submitted to the driver on issue rather than left for a resolve to
    // submit. draw_calls above minus (size + kick) is what the old
    // resolve-driven policy would have issued on its own.
    {
        const auto fs = nv->witnessed_flush_stats();
        std::printf(
            "[host] gpu-executor-flush scope=%s deadline=%llu bound=%llu "
            "size_flushes=%llu kick_flushes=%llu gl_flushes=%llu "
            "vblank_kick=%u ssbo_ring=%llu kick_flush=%u fp32_shader=%u "
            "plane_split=%u\n",
            scope, deadline, (unsigned long long)fs.bound,
            (unsigned long long)fs.size_flushes,
            (unsigned long long)fs.kick_flushes,
            (unsigned long long)fs.gl_flushes,
            nv->gpu_vblank_kick() ? 1u : 0u,
            (unsigned long long)fs.ssbo_ring, fs.kick_flush ? 1u : 0u,
            fs.fp32_shader ? 1u : 0u, fs.plane_split ? 1u : 0u);
    }
    std::printf(
        "[host] gpu-executor-surfaces scope=%s deadline=%llu "
        "radial_color=%08x radial_zeta=%08x radial_bytes=%u "
        "textured_color=%08x textured_zeta=%08x textured_bytes=%u "
        "projected_color=%08x projected_zeta=%08x projected_bytes=%u\n",
        scope, deadline, nv->gpu_profile_surface(0, false),
        nv->gpu_profile_surface(0, true), nv->gpu_profile_surface_bytes(0),
        nv->gpu_profile_surface(1, false), nv->gpu_profile_surface(1, true),
        nv->gpu_profile_surface_bytes(1),
        nv->gpu_profile_surface(2, false), nv->gpu_profile_surface(2, true),
        nv->gpu_profile_surface_bytes(2));
    for (size_t k = 0; k < xlle::Nv2a::kGpuRejectReasons; ++k) {
        const uint64_t n = nv->gpu_profile_reject(k);
        if (n == 0) continue;
        std::printf(
            "[host] gpu-executor-reject scope=%s deadline=%llu reason=%s "
            "jobs=%llu\n",
            scope, deadline, xlle::Nv2a::gpu_reject_reason_name(k),
            (unsigned long long)n);
    }
    // beads-xbox.1.15.10 phase 4 (lever B). Always on: how many witnessed
    // texture snapshots asked for a resolve because their source range could be
    // a GPU-authoritative surface, and how many the ceiling probe skipped.
    std::printf(
        "[host] gpu-texture-source scope=%s deadline=%llu resolves=%llu "
        "skipped=%llu unsafe_skip=%u\n",
        scope, deadline,
        (unsigned long long)nv->witnessed_texture_source_resolves(),
        (unsigned long long)nv->witnessed_texture_source_skipped(),
        nv->gpu_unsafe_skip_texture_resolve() ? 1u : 0u);
    if (nv->gpu_profile_max_fixed() != 0) {
        std::printf(
            "[host] gpu-executor-range scope=%s deadline=%llu "
            "max_fixed_xy=%llu limit=%llu\n",
            scope, deadline, (unsigned long long)nv->gpu_profile_max_fixed(),
            (unsigned long long)(nv->gpu_exact_coords()
                                     ? xlle::Nv2a::kGpuFixedCoordinateLimitExact
                                     : xlle::Nv2a::kGpuFixedCoordinateLimitRelaxed));
    }
    for (size_t k = 0; k < xlle::Nv2a::kGpuResidencyCauses; ++k) {
        std::printf(
            "[host] gpu-residency-cause scope=%s deadline=%llu cause=%s "
            "resolves=%llu\n",
            scope, deadline, xlle::Nv2a::gpu_residency_cause_name(k),
            (unsigned long long)c.cause[k]);
    }
    // Phase 2 work-order census (beads-xbox.1.15.10). Two columns per profile:
    // cpu_draws is the CPU work left, breaks is the round trips that work
    // actually costs. They are not proportional and the work order follows
    // breaks; both are printed so the reader can see the difference.
    for (size_t k = 0; k < xlle::Nv2a::kGpuResProfileSlots; ++k) {
        if (c.cpu_draws_profile[k] == 0 && c.breaks_profile[k] == 0) continue;
        std::printf(
            "[host] gpu-residency-profile scope=%s deadline=%llu profile=%s "
            "cpu_draws=%llu breaks=%llu\n",
            scope, deadline, xlle::Nv2a::gpu_res_profile_name(k),
            (unsigned long long)c.cpu_draws_profile[k],
            (unsigned long long)c.breaks_profile[k]);
    }
    // Phase 5 item 2 (beads-xbox.1.15.10): what a residency QUERY costs, split
    // by whether it moved any bytes. ALWAYS ON. This is the line that decides
    // whether the "148 317 no-op residency queries, about 0.9 s" item is real:
    // noop_n x noop_ns is the whole of what a per-draw fast path could remove,
    // measured directly rather than inferred from a ceiling probe minus a
    // byte-rate model.
    {
        const auto& q = nv->resolve_query_census();
        const auto ns = [](unsigned long long ticks) {
            return xlle::perf_excl::ticks_to_ns(ticks);
        };
        const auto per = [](unsigned long long total_ns,
                            unsigned long long n) {
            return n ? (double)total_ns / (double)n : 0.0;
        };
        const unsigned long long cn = ns(q.color_noop_ticks);
        const unsigned long long cm = ns(q.color_moved_ticks);
        const unsigned long long dn = ns(q.depth_noop_ticks);
        const unsigned long long dm = ns(q.depth_moved_ticks);
        const unsigned long long tn = ns(q.texsrc_noop_ticks);
        const unsigned long long tm = ns(q.texsrc_moved_ticks);
        std::printf(
            "[host] gpu-resolve-cost scope=%s deadline=%llu "
            "color_noop_n=%llu color_noop_ns=%llu color_noop_ns_each=%.1f "
            "color_moved_n=%llu color_moved_ns=%llu color_moved_ns_each=%.1f "
            "depth_noop_n=%llu depth_noop_ns=%llu depth_noop_ns_each=%.1f "
            "depth_moved_n=%llu depth_moved_ns=%llu depth_moved_ns_each=%.1f "
            "texsrc_noop_n=%llu texsrc_noop_ns=%llu texsrc_noop_ns_each=%.1f "
            "texsrc_moved_n=%llu texsrc_moved_ns=%llu texsrc_moved_ns_each=%.1f "
            "color_backend_ns=%llu color_memwrite_ns=%llu color_memwrite_mb=%.1f "
            "depth_backend_ns=%llu depth_memwrite_ns=%llu depth_memwrite_mb=%.1f\n",
            scope, deadline,
            (unsigned long long)q.color_noop_n, cn,
            per(cn, q.color_noop_n),
            (unsigned long long)q.color_moved_n, cm,
            per(cm, q.color_moved_n),
            (unsigned long long)q.depth_noop_n, dn,
            per(dn, q.depth_noop_n),
            (unsigned long long)q.depth_moved_n, dm,
            per(dm, q.depth_moved_n),
            (unsigned long long)q.texsrc_noop_n, tn,
            per(tn, q.texsrc_noop_n),
            (unsigned long long)q.texsrc_moved_n, tm,
            per(tm, q.texsrc_moved_n),
            ns(q.color_backend_ticks), ns(q.color_memwrite_ticks),
            q.color_memwrite_bytes / 1048576.0,
            ns(q.depth_backend_ticks), ns(q.depth_memwrite_ticks),
            q.depth_memwrite_bytes / 1048576.0);
    }
    // Phase 5 item 3 (beads-xbox.1.15.10): the vertex-pipeline split. parse_*
    // is always on; fetch_* / vsh_* are zero unless --nv2a-vertex-census armed
    // them, and the line says which by reporting their denominators.
    {
        const auto& v = nv->vertex_census();
        const auto ns = [](unsigned long long ticks) {
            return xlle::perf_excl::ticks_to_ns(ticks);
        };
        std::printf(
            "[host] nv2a-vertex-census scope=%s deadline=%llu batches=%llu "
            "vertices=%llu reuse_hits=%llu parses=%llu parse_reuses=%llu "
            "parse_ns=%llu fetch_n=%llu fetch_ns=%llu vsh_n=%llu vsh_ns=%llu "
            "vsh_context_vertices=%llu fixed_function_vertices=%llu "
            "program_cache=%u fast_fetch=%u census=%u\n",
            scope, deadline, (unsigned long long)v.batches,
            (unsigned long long)v.vertices, (unsigned long long)v.reuse_hits,
            (unsigned long long)v.parses, (unsigned long long)v.parse_reuses,
            ns(v.parse_ticks), (unsigned long long)v.fetch_n,
            ns(v.fetch_ticks), (unsigned long long)v.vsh_n, ns(v.vsh_ticks),
            (unsigned long long)v.vsh_context_vertices,
            (unsigned long long)v.fixed_function_vertices,
            nv->vertex_program_cache_enabled() ? 1u : 0u,
            nv->vertex_fast_fetch_enabled() ? 1u : 0u,
            nv->vertex_census_enabled() ? 1u : 0u);
        // beads-xbox.1.15.11: which of the two step forms actually ran, and
        // what the pre-decode itself cost. ALWAYS ON, because a compiled-path
        // claim without its own denominator is not a measurement.
        std::printf(
            "[host] nv2a-vsh-form scope=%s deadline=%llu compiled_vertices=%llu "
            "interpreted_vertices=%llu compiles=%llu declines=%llu "
            "compile_ns=%llu compile=%u\n",
            scope, deadline,
            (unsigned long long)v.vsh_compiled_vertices,
            (unsigned long long)v.vsh_interpreted_vertices,
            (unsigned long long)v.vsh_compiles,
            (unsigned long long)v.vsh_compile_declines,
            ns(v.vsh_compile_ticks), nv->vsh_compile_enabled() ? 1u : 0u);
        std::printf(
            "[host] nv2a-vsh-verify scope=%s deadline=%llu budget=%u "
            "checked=%llu mismatched=%llu\n",
            scope, deadline, nv->vsh_verify_budget(),
            (unsigned long long)nv->vsh_verify_checked(),
            (unsigned long long)nv->vsh_verify_mismatched());
    }
    // beads-xbox.1.15.11: the DISTINCT-program census. The compiled step form
    // is amortised over every vertex a program runs for, so the lever exists
    // only if a handful of programs carry the vertices. One row per distinct
    // (program words, start slot) pair; overflow is counted, never evicted.
    {
        const auto& census = nv->vsh_program_census();
        std::printf(
            "[host] nv2a-vsh-program-census scope=%s deadline=%llu rows=%llu "
            "overflow=%llu\n",
            scope, deadline, (unsigned long long)census.count,
            (unsigned long long)census.overflow);
        for (size_t i = 0; i < census.count; ++i) {
            const auto& row = census.rows[i];
            std::printf(
                "[host] nv2a-vsh-program scope=%s deadline=%llu index=%llu "
                "fp=%016llx start=%u slots=%u steps=%u ops=%u paired=%u "
                "compiled=%u writes_context=%u zero_outputs=%u zero_temps=%u "
                "zero_address=%u batches=%llu vertices=%llu\n",
                scope, deadline, (unsigned long long)i,
                (unsigned long long)row.fingerprint, row.start_slot, row.slots,
                row.steps, row.ops, row.paired, row.compiled ? 1u : 0u,
                row.writes_context ? 1u : 0u, row.zero_outputs, row.zero_temps,
                row.zero_address, (unsigned long long)row.batches,
                (unsigned long long)row.vertices);
        }
    }
    // Phase 5 item 1 (beads-xbox.1.15.10): the per-PLANE consumer table. The
    // phase-4 rule is that a break is attributed to whichever CPU consumer
    // touches the plane first, so `gpu-residency-profile` above cannot answer
    // "which families would I have to move to stop resolving this plane".
    // This can: one row per destination plane, with the GPU-taken and
    // CPU-executed draw counts and the CPU families listed by name. A plane
    // whose cpu_draws is 0 has no CPU consumer left and its resolves are gone.
    {
        struct PlaneTable {
            const char* plane;
            const xlle::Nv2a::GpuResSurfaceTable* table;
        };
        const PlaneTable tables[2] = {
            {"color", &nv->gpu_res_color_surfaces()},
            {"zeta", &nv->gpu_res_depth_surfaces()},
        };
        for (const auto& entry : tables) {
            for (size_t r = 0; r < entry.table->count; ++r) {
                const auto& row = entry.table->rows[r];
                char families[512];
                size_t used = 0;
                families[0] = '\0';
                for (size_t k = 0; k < xlle::Nv2a::kGpuResProfileSlots; ++k) {
                    if (row.cpu_draws_profile[k] == 0) continue;
                    const int n = std::snprintf(
                        families + used, sizeof(families) - used, "%s%s:%llu",
                        used ? "," : "", xlle::Nv2a::gpu_res_profile_name(k),
                        (unsigned long long)row.cpu_draws_profile[k]);
                    if (n <= 0 || used + (size_t)n >= sizeof(families)) break;
                    used += (size_t)n;
                }
                std::printf(
                    "[host] gpu-residency-plane scope=%s deadline=%llu "
                    "plane=%s address=%08x bytes=%u gpu_draws=%llu "
                    "cpu_draws=%llu cpu_families=%s\n",
                    scope, deadline, entry.plane, row.address, row.bytes,
                    (unsigned long long)row.gpu_draws,
                    (unsigned long long)row.cpu_draws,
                    used ? families : "-");
            }
            if (entry.table->overflow) {
                std::printf(
                    "[host] gpu-residency-plane scope=%s deadline=%llu "
                    "plane=%s address=OVERFLOW bytes=0 gpu_draws=0 "
                    "cpu_draws=%llu cpu_families=-\n",
                    scope, deadline, entry.plane,
                    (unsigned long long)entry.table->overflow);
            }
        }
    }
    // Phase 6 (beads-xbox.1.15.10): the generic-combiner SHADER census. One row
    // per distinct generated-fragment-program state, with the transferring
    // resolves that state's software fallback actually caused. This is the work
    // order for the generator: phase 5 measured that 1 307 of the 1 954 colour
    // resolves are generic-combiner paths but could not say which STATES, and a
    // census keyed on the whole raster block (the --nv2a-detailed-telemetry
    // fingerprint table) splits one program across many rows. Always on.
    {
        const auto& g = nv->generic_shader_census();
        for (size_t r = 0; r < g.count; ++r) {
            const auto& row = g.rows[r];
            std::printf(
                "[host] generic-shader scope=%s deadline=%llu fp=%016llx "
                "batches=%llu sw=%llu gl=%llu gpu=%llu tri=%llu bbox=%llu "
                "verts=%llu "
                "color_resolves=%llu depth_resolves=%llu tex_resolves=%llu "
                "reject=%s stages=%u cc=%08x rs0=%08x osi=%08x fmt=%08x "
                "swz=%u depth=%u dfunc=%04x dmask=%u afunc=%04x aref=%u "
                "bsrc=%04x bdst=%04x beq=%04x cmask=%08x cull=%04x "
                "texref=%x texcube=%x texlin=%x texa8=%x texen=%x prim=%x\n",
                scope, deadline, (unsigned long long)row.fingerprint,
                (unsigned long long)row.batches,
                (unsigned long long)row.software_batches,
                (unsigned long long)row.accelerated_batches,
                (unsigned long long)row.gpu_batches,
                (unsigned long long)row.triangles,
                (unsigned long long)row.bbox_pixels,
                (unsigned long long)row.vertices,
                (unsigned long long)row.color_resolves,
                (unsigned long long)row.depth_resolves,
                (unsigned long long)row.texture_resolves,
                row.reject_reason >=
                        xlle::Nv2a::generic_resident_reject_reason_count()
                    ? "not-observed"
                    : xlle::Nv2a::generic_resident_reject_reason_name(
                          row.reject_reason),
                row.stage_count, row.combiner_control, row.shader_stage_program,
                row.other_stage_input, row.surface_format,
                row.swizzled_destination, row.uses_depth, row.depth_func,
                row.depth_mask, row.alpha_func, row.alpha_ref, row.blend_src,
                row.blend_dst, row.blend_equation, row.color_mask,
                row.cull_face, row.texture_referenced, row.texture_cubemap,
                row.texture_linear, row.texture_alpha8,
                row.texture_enable_mask, row.primitive_modes);
            // The combiner words themselves, on their own line so the shader a
            // row needs can be read out of the log without a second run.
            std::printf(
                "[host] generic-shader-words scope=%s deadline=%llu "
                "fp=%016llx cicw=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
                "aicw=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
                "cocw=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
                "aocw=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
                "f0=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
                "f1=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
                "sf=%08x,%08x ff=%08x,%08x "
                "texfmt=%08x,%08x,%08x,%08x texaddr=%08x,%08x,%08x,%08x "
                "texfilt=%08x,%08x,%08x,%08x "
                "texdim=%ux%u,%ux%u,%ux%u,%ux%u\n",
                scope, deadline, (unsigned long long)row.fingerprint,
                row.color_icw[0], row.color_icw[1], row.color_icw[2],
                row.color_icw[3], row.color_icw[4], row.color_icw[5],
                row.color_icw[6], row.color_icw[7],
                row.alpha_icw[0], row.alpha_icw[1], row.alpha_icw[2],
                row.alpha_icw[3], row.alpha_icw[4], row.alpha_icw[5],
                row.alpha_icw[6], row.alpha_icw[7],
                row.color_ocw[0], row.color_ocw[1], row.color_ocw[2],
                row.color_ocw[3], row.color_ocw[4], row.color_ocw[5],
                row.color_ocw[6], row.color_ocw[7],
                row.alpha_ocw[0], row.alpha_ocw[1], row.alpha_ocw[2],
                row.alpha_ocw[3], row.alpha_ocw[4], row.alpha_ocw[5],
                row.alpha_ocw[6], row.alpha_ocw[7],
                row.factor0[0], row.factor0[1], row.factor0[2], row.factor0[3],
                row.factor0[4], row.factor0[5], row.factor0[6], row.factor0[7],
                row.factor1[0], row.factor1[1], row.factor1[2], row.factor1[3],
                row.factor1[4], row.factor1[5], row.factor1[6], row.factor1[7],
                row.specular_fog0, row.specular_fog1, row.final_factor0,
                row.final_factor1,
                row.tex_format[0], row.tex_format[1], row.tex_format[2],
                row.tex_format[3], row.tex_address[0], row.tex_address[1],
                row.tex_address[2], row.tex_address[3], row.tex_filter[0],
                row.tex_filter[1], row.tex_filter[2], row.tex_filter[3],
                row.tex_width[0], row.tex_height[0], row.tex_width[1],
                row.tex_height[1], row.tex_width[2], row.tex_height[2],
                row.tex_width[3], row.tex_height[3]);
            // Which destination plane the row's batches and resolves belong to.
            std::printf(
                "[host] generic-shader-dest scope=%s deadline=%llu fp=%016llx "
                "d0=%08x/%llu/%llu d1=%08x/%llu/%llu d2=%08x/%llu/%llu "
                "d3=%08x/%llu/%llu overflow=%llu\n",
                scope, deadline, (unsigned long long)row.fingerprint,
                row.dest_address[0], (unsigned long long)row.dest_batches[0],
                (unsigned long long)row.dest_resolves[0],
                row.dest_address[1], (unsigned long long)row.dest_batches[1],
                (unsigned long long)row.dest_resolves[1],
                row.dest_address[2], (unsigned long long)row.dest_batches[2],
                (unsigned long long)row.dest_resolves[2],
                row.dest_address[3], (unsigned long long)row.dest_batches[3],
                (unsigned long long)row.dest_resolves[3],
                (unsigned long long)row.dest_overflow);
        }
        std::printf(
            "[host] generic-shader-total scope=%s deadline=%llu rows=%zu "
            "overflow_batches=%llu overflow_color_resolves=%llu\n",
            scope, deadline, g.count,
            (unsigned long long)g.overflow_batches,
            (unsigned long long)g.overflow_color_resolves);
    }
    // Phase 6 (beads-xbox.1.15.10): what the GENERATED-shader executor took and
    // why every batch it declined was declined. Always on, and the decline
    // table covers both halves -- the device's own limits and the generator's
    // own unsupported-state answer -- so no decline is unattributed.
    {
        const auto d = nv->generated_shader_census();
        const auto r = nv->generated_shader_backend_stats();
        std::printf(
            "[host] generic-shader-gpu scope=%s deadline=%llu enabled=%u "
            "attempts=%llu submits=%llu empty=%llu triangles=%llu "
            "color_upload_mb=%.1f texture_upload_mb=%.1f programs=%llu "
            "compiles=%llu compile_failures=%llu cache_hits=%llu draws=%llu "
            "backend_triangles=%llu tex_uploads=%llu tex_skips=%llu "
            "surface_uploads=%llu\n",
            scope, deadline, nv->gpu_generic_shader() ? 1u : 0u,
            (unsigned long long)d.attempts, (unsigned long long)d.submits,
            (unsigned long long)d.empty_batches,
            (unsigned long long)d.triangles,
            d.color_upload_bytes / 1048576.0,
            d.texture_upload_bytes / 1048576.0,
            (unsigned long long)r.programs, (unsigned long long)r.compiles,
            (unsigned long long)r.compile_failures,
            (unsigned long long)r.cache_hits, (unsigned long long)r.draws,
            (unsigned long long)r.triangles,
            (unsigned long long)r.texture_uploads,
            (unsigned long long)r.texture_upload_skips,
            (unsigned long long)r.surface_uploads);
        for (size_t k = 0; k < nv->generic_shader_program_count(); ++k) {
            std::printf(
                "[host] generic-shader-program scope=%s deadline=%llu "
                "index=%zu bit=%016llx fp=%016llx batches=%llu\n",
                scope, deadline, k,
                (unsigned long long)(uint64_t{1} << k),
                (unsigned long long)nv->generic_shader_program_fingerprint(k),
                (unsigned long long)nv->generic_shader_program_batches(k));
        }
        for (size_t k = 0; k < xlle::Nv2a::generic_shader_decline_count() &&
                           k < d.declines.size();
             ++k) {
            if (d.declines[k] == 0) continue;
            std::printf(
                "[host] generic-shader-decline scope=%s deadline=%llu "
                "reason=%s batches=%llu\n",
                scope, deadline, xlle::Nv2a::generic_shader_decline_name(k),
                (unsigned long long)d.declines[k]);
        }
    }
    // Phase 3 (beads-xbox.1.15.10): the MEASURED residency-break attribution,
    // taken from the BACKEND rather than from the simulation above. One row per
    // (cause, profile) pair that actually caused a re-upload, split by plane.
    // The simulation reported 647 breaks where the backend performed 1336
    // uploads; this is the line that says which consumer the difference belongs
    // to, so the transfer-volume work is ordered by measurement and not by a
    // model whose one-active-target assumption is known to be wrong.
    {
        const auto uc = nv->witnessed_upload_census();
        unsigned long long total_color = 0, total_depth = 0;
        double total_color_mb = 0.0, total_depth_mb = 0.0;
        for (size_t cause = 0; cause < xlle::kNv2aInvalidateCauseSlots; ++cause) {
            for (size_t prof = 0; prof < xlle::kNv2aInvalidateProfileSlots;
                 ++prof) {
                const unsigned long long cn =
                    (unsigned long long)uc.color_uploads[cause][prof];
                const unsigned long long dn =
                    (unsigned long long)uc.depth_uploads[cause][prof];
                total_color += cn;
                total_depth += dn;
                total_color_mb += uc.color_bytes[cause][prof] / 1048576.0;
                total_depth_mb += uc.depth_bytes[cause][prof] / 1048576.0;
                if (cn == 0 && dn == 0) continue;
                std::printf(
                    "[host] gpu-upload-cause scope=%s deadline=%llu cause=%s "
                    "profile=%s color_uploads=%llu color_mb=%.1f "
                    "depth_uploads=%llu depth_mb=%.1f\n",
                    scope, deadline, xlle::nv2a_invalidate_cause_name(cause),
                    xlle::Nv2a::gpu_res_profile_name(prof), cn,
                    uc.color_bytes[cause][prof] / 1048576.0, dn,
                    uc.depth_bytes[cause][prof] / 1048576.0);
            }
        }
        for (size_t cause = 0; cause < xlle::kNv2aInvalidateCauseSlots; ++cause) {
            const auto& ce = uc.color_invalidate_extent[cause];
            const auto& de = uc.depth_invalidate_extent[cause];
            if (ce[0] + ce[1] + ce[2] + de[0] + de[1] + de[2] == 0) continue;
            std::printf(
                "[host] gpu-invalidate-extent scope=%s deadline=%llu cause=%s "
                "color_exact=%llu color_cover=%llu color_partial=%llu "
                "zeta_exact=%llu zeta_cover=%llu zeta_partial=%llu\n",
                scope, deadline, xlle::nv2a_invalidate_cause_name(cause),
                (unsigned long long)ce[0], (unsigned long long)ce[1],
                (unsigned long long)ce[2], (unsigned long long)de[0],
                (unsigned long long)de[1], (unsigned long long)de[2]);
        }
        std::printf(
            "[host] gpu-clear-backend scope=%s deadline=%llu accept_color=%llu "
            "accept_zeta=%llu missing_color=%llu missing_zeta=%llu "
            "over_dirty_color=%llu over_dirty_zeta=%llu key_mismatch_color=%llu "
            "key_mismatch_zeta=%llu stamp_overflows=%llu "
            "takeover_color=%llu takeover_zeta=%llu "
            "plane_split_hits=%llu plane_split_uploads=%llu\n",
            scope, deadline, (unsigned long long)uc.clear_accept_color,
            (unsigned long long)uc.clear_accept_depth,
            (unsigned long long)uc.clear_decline_color_missing,
            (unsigned long long)uc.clear_decline_depth_missing,
            (unsigned long long)uc.upload_over_dirty_color,
            (unsigned long long)uc.upload_over_dirty_depth,
            (unsigned long long)uc.color_key_mismatch,
            (unsigned long long)uc.depth_key_mismatch,
            (unsigned long long)uc.stamp_overflows,
            (unsigned long long)uc.color_slot_takeover,
            (unsigned long long)uc.depth_slot_takeover,
            (unsigned long long)uc.plane_split_hits,
            (unsigned long long)uc.plane_split_uploads);
        std::printf(
            "[host] gpu-clear scope=%s deadline=%llu enabled=%u accepted=%llu "
            "mb=%.1f decline_planes=%llu decline_mask=%llu "
            "decline_geometry=%llu decline_resident=%llu kept_clean=%llu "
            "skip_empty=%u "
            "empty_packets=%llu\n",
            scope, deadline, nv->gpu_clear_enabled() ? 1u : 0u,
            (unsigned long long)nv->gpu_clear_accepted(),
            nv->gpu_clear_bytes() / 1048576.0,
            (unsigned long long)nv->gpu_clear_decline_planes(),
            (unsigned long long)nv->gpu_clear_decline_mask(),
            (unsigned long long)nv->gpu_clear_decline_geometry(),
            (unsigned long long)nv->gpu_clear_decline_resident(),
            (unsigned long long)nv->gpu_clear_kept_clean(),
            nv->gpu_skip_empty_packets() ? 1u : 0u,
            (unsigned long long)nv->gpu_profile_empty_packets());
        std::printf(
            "[host] gpu-upload-total scope=%s deadline=%llu color_uploads=%llu "
            "color_mb=%.1f depth_uploads=%llu depth_mb=%.1f "
            "device_color_uploads=%llu device_depth_uploads=%llu "
            "device_pair_uploads=%llu row_uploads=%llu row_rows=%llu "
            "row_mb=%.1f row_surface_rows=%llu\n",
            scope, deadline, total_color, total_color_mb, total_depth,
            total_depth_mb,
            (unsigned long long)nv->gpu_profile_color_uploads(),
            (unsigned long long)nv->gpu_profile_depth_uploads(),
            (unsigned long long)nv->gpu_profile_uploads(),
            (unsigned long long)uc.row_uploads,
            (unsigned long long)uc.row_upload_rows,
            uc.row_upload_bytes / 1048576.0,
            (unsigned long long)uc.row_upload_surface_rows);
    }
}

void print_seam_census(const xlle::Nv2a* nv, const char* scope,
                       unsigned long long deadline, SeamCensusSnapshot& prev) {
    if (!nv) return;
    SeamCensusSnapshot now;
    now.jobs_latched = nv->seam_jobs_latched();
    now.jobs_threadable = nv->seam_jobs_threadable();
    now.jobs_queueable = nv->seam_jobs_queueable();
    now.jobs_not_executed = nv->seam_jobs_not_executed();
    now.vblanks = nv->seam_vblanks();
    now.vblank_jobs_sum = nv->seam_vblank_jobs_sum();
    now.resolve_transfers = nv->seam_resolve_transfers();
    for (size_t r = 0; r < xlle::Nv2a::kSeamNotThreadableReasons; ++r)
        now.not_threadable[r] = nv->seam_not_threadable(static_cast<uint32_t>(r));
    for (size_t a = 0; a < xlle::Nv2a::kSeamAnchorCount; ++a) {
        now.hits[a] = nv->seam_anchor_hits(a);
        now.drains[a] = nv->seam_anchor_drains(a);
        now.pending_sum[a] = nv->seam_anchor_pending_sum(a);
    }
    now.jobs_queued = nv->draw_jobs_queued();
    now.jobs_inline = nv->draw_jobs_inline();
    now.backpressure_inline = nv->draw_backpressure_inline();
    now.backpressure_bytes = nv->draw_backpressure_bytes();
    now.order_fences = nv->draw_order_fences();
    now.drains_empty = nv->draw_drains_empty();
    now.drains_nonempty = nv->draw_drains_nonempty();
    now.depth_sum = nv->draw_depth_sum();
    now.depth_samples = nv->draw_depth_samples();
    now.worker_busy_ticks = nv->draw_worker_busy_ticks();
    now.drain_wait_ticks = nv->draw_drain_wait_ticks();
    now.elapsed_ticks = nv->draw_elapsed_ticks();
    now.worker_failures = nv->draw_worker_failures();
    for (size_t b = 0; b < xlle::Nv2a::kDrawDepthBuckets; ++b)
        now.depth_hist[b] = nv->draw_depth_bucket(b);
    now.span_jobs = nv->draw_span_jobs();
    now.span_inline_jobs = nv->draw_span_inline_jobs();
    now.span_barrier_wait_ticks = nv->draw_span_barrier_wait_ticks();
    now.span_part0_busy_ticks = nv->draw_span_part0_busy_ticks();
    now.span_helper_busy_ticks = nv->draw_span_helper_busy_ticks();
    now.span_fold_ticks = nv->draw_span_fold_ticks();
    now.renderer_query_fences = nv->draw_renderer_query_fences();
    const uint64_t latched = now.jobs_latched - prev.jobs_latched;
    const uint64_t threadable = now.jobs_threadable - prev.jobs_threadable;
    const uint64_t executed_jobs =
        latched - (now.jobs_not_executed - prev.jobs_not_executed);
    const uint64_t vblanks = now.vblanks - prev.vblanks;
    std::printf(
        "[host] seam-census scope=%s deadline=%llu jobs_latched=%llu "
        "jobs_executed=%llu jobs_threadable=%llu threadable_pct=%.3f "
        "jobs_queueable=%llu queueable_pct=%.3f "
        "jobs_not_executed=%llu nt_no_direct_ptr=%llu nt_hash_on=%llu "
        "nt_f3_renderer_path=%llu nt_f15_debug_cb=%llu vblanks=%llu "
        "vblank_jobs_sum=%llu vblank_jobs_max=%llu resolve_transfers=%llu\n",
        scope, deadline,
        (unsigned long long)latched,
        (unsigned long long)executed_jobs,
        (unsigned long long)threadable,
        executed_jobs ? 100.0 * (double)threadable / (double)executed_jobs : 0.0,
        (unsigned long long)(now.jobs_queueable - prev.jobs_queueable),
        executed_jobs
            ? 100.0 * (double)(now.jobs_queueable - prev.jobs_queueable) /
                  (double)executed_jobs
            : 0.0,
        (unsigned long long)(now.jobs_not_executed - prev.jobs_not_executed),
        (unsigned long long)(now.not_threadable[xlle::Nv2a::kSeamNoDirectPointer] -
                             prev.not_threadable[xlle::Nv2a::kSeamNoDirectPointer]),
        (unsigned long long)(now.not_threadable[xlle::Nv2a::kSeamHashOn] -
                             prev.not_threadable[xlle::Nv2a::kSeamHashOn]),
        (unsigned long long)(now.not_threadable[xlle::Nv2a::kSeamRendererPath] -
                             prev.not_threadable[xlle::Nv2a::kSeamRendererPath]),
        (unsigned long long)(now.not_threadable[xlle::Nv2a::kSeamDebugCallback] -
                             prev.not_threadable[xlle::Nv2a::kSeamDebugCallback]),
        (unsigned long long)vblanks,
        (unsigned long long)(now.vblank_jobs_sum - prev.vblank_jobs_sum),
        (unsigned long long)nv->seam_vblank_jobs_max(),
        (unsigned long long)(now.resolve_transfers - prev.resolve_transfers));
    // One line per anchor so the dominant anchor is visible rather than summed
    // away: hits = the site was reached, drains = a threaded consumer would have
    // had to stop there, pending = jobs it would have been waiting on.
    for (size_t a = 0; a < xlle::Nv2a::kSeamAnchorCount; ++a) {
        const uint64_t hits = now.hits[a] - prev.hits[a];
        const uint64_t drains = now.drains[a] - prev.drains[a];
        const uint64_t psum = now.pending_sum[a] - prev.pending_sum[a];
        std::printf(
            "[host] seam-anchor scope=%s deadline=%llu anchor=%s hits=%llu "
            "drains=%llu pending_sum=%llu pending_mean=%.3f pending_max=%llu "
            "drains_per_vblank=%.3f\n",
            scope, deadline, xlle::Nv2a::seam_anchor_name(a),
            (unsigned long long)hits, (unsigned long long)drains,
            (unsigned long long)psum,
            drains ? (double)psum / (double)drains : 0.0,
            (unsigned long long)nv->seam_anchor_pending_max(a),
            vblanks ? (double)drains / (double)vblanks : 0.0);
    }
    // Stage (e) (beads-xbox.1.15.5): the REAL queue. jobs_queued is the witness
    // that threading actually ran -- a gate that passes with jobs_queued=0 is a
    // gate that tested nothing. Ticks are raw rdtsc; the fractions are against
    // draw_elapsed_ticks (wall since the worker was created), which is what makes
    // them comparable across legs without needing a calibrated ns/tick.
    const uint64_t queued = now.jobs_queued - prev.jobs_queued;
    const uint64_t depth_samples = now.depth_samples - prev.depth_samples;
    const uint64_t depth_sum = now.depth_sum - prev.depth_sum;
    const uint64_t drains_empty = now.drains_empty - prev.drains_empty;
    const uint64_t drains_nonempty = now.drains_nonempty - prev.drains_nonempty;
    const uint64_t busy = now.worker_busy_ticks - prev.worker_busy_ticks;
    const uint64_t waited = now.drain_wait_ticks - prev.drain_wait_ticks;
    const uint64_t elapsed = now.elapsed_ticks - prev.elapsed_ticks;
    std::printf(
        "[host] draw-queue scope=%s deadline=%llu threads=%u jobs_queued=%llu "
        "jobs_inline=%llu backpressure_inline=%llu drains_empty=%llu "
        "drains_nonempty=%llu queue_depth_mean=%.3f queue_depth_max=%llu "
        "backpressure_bytes=%llu queue_bytes_max=%llu order_fences=%llu "
        "worker_busy_ticks=%llu drain_wait_ticks=%llu elapsed_ticks=%llu "
        "worker_busy_frac=%.4f drain_wait_frac=%.4f worker_failures=%llu "
        "renderer_query_fences=%llu\n",
        scope, deadline, nv->draw_threads(),
        (unsigned long long)queued,
        (unsigned long long)(now.jobs_inline - prev.jobs_inline),
        (unsigned long long)(now.backpressure_inline - prev.backpressure_inline),
        (unsigned long long)drains_empty,
        (unsigned long long)drains_nonempty,
        depth_samples ? (double)depth_sum / (double)depth_samples : 0.0,
        (unsigned long long)nv->draw_depth_max(),
        (unsigned long long)(now.backpressure_bytes - prev.backpressure_bytes),
        (unsigned long long)nv->draw_queue_bytes_max(),
        (unsigned long long)(now.order_fences - prev.order_fences),
        (unsigned long long)busy,
        (unsigned long long)waited,
        (unsigned long long)elapsed,
        elapsed ? (double)busy / (double)elapsed : 0.0,
        elapsed ? (double)waited / (double)elapsed : 0.0,
        (unsigned long long)(now.worker_failures - prev.worker_failures),
        (unsigned long long)(now.renderer_query_fences -
                             prev.renderer_query_fences));
    for (size_t b = 0; b < xlle::Nv2a::kDrawDepthBuckets; ++b) {
        std::printf("[host] draw-depth scope=%s deadline=%llu bucket=%s count=%llu\n",
                    scope, deadline, xlle::Nv2a::draw_depth_bucket_name(b),
                    (unsigned long long)(now.depth_hist[b] - prev.depth_hist[b]));
    }
    // Stage (e2) (beads-xbox.1.15.9). Its own line rather than more fields on
    // draw-queue, so the existing DRAW_QUEUE_RE parser is untouched. threads is
    // the configured part count N; span_jobs counts draws that were divided
    // across all N; span_inline counts draws that ran whole on the emulation
    // thread because they are not threadable. barrier_wait_ticks is the
    // emulation thread's time waiting for the helper parts AFTER finishing its
    // own -- i.e. the mechanism's direct cost and its load-imbalance together.
    // helper_busy_ticks is summed across the N-1 helpers, so the per-helper duty
    // cycle is helper_busy / ((N-1) * elapsed).
    {
        const uint64_t span_jobs = now.span_jobs - prev.span_jobs;
        const uint64_t barrier = now.span_barrier_wait_ticks -
                                 prev.span_barrier_wait_ticks;
        const uint64_t part0 = now.span_part0_busy_ticks -
                               prev.span_part0_busy_ticks;
        const uint64_t helper = now.span_helper_busy_ticks -
                                prev.span_helper_busy_ticks;
        const unsigned helpers =
            nv->draw_threads() > 1 ? nv->draw_threads() - 1 : 0;
        std::printf(
            "[host] draw-span scope=%s deadline=%llu threads=%u span_jobs=%llu "
            "span_inline=%llu barrier_wait_ticks=%llu part0_busy_ticks=%llu "
            "helper_busy_ticks=%llu elapsed_ticks=%llu barrier_frac=%.4f "
            "part0_busy_frac=%.4f helper_busy_frac=%.4f "
            "barrier_ticks_per_job=%.1f fold_ticks=%llu\n",
            scope, deadline, nv->draw_threads(),
            (unsigned long long)span_jobs,
            (unsigned long long)(now.span_inline_jobs - prev.span_inline_jobs),
            (unsigned long long)barrier, (unsigned long long)part0,
            (unsigned long long)helper, (unsigned long long)elapsed,
            elapsed ? (double)barrier / (double)elapsed : 0.0,
            elapsed ? (double)part0 / (double)elapsed : 0.0,
            (elapsed && helpers) ? (double)helper / ((double)elapsed * helpers)
                                 : 0.0,
            span_jobs ? (double)barrier / (double)span_jobs : 0.0,
            (unsigned long long)(now.span_fold_ticks - prev.span_fold_ticks));
    }
    // ---- worker_raster_ns: the SIBLING line, not a partition bucket ---------
    // beads-xbox.1.15.9 loose end 1. [perf-excl]'s denominator is EMULATION-
    // THREAD wall and it currently sums to ~99.94 % of it; folding helper time
    // into it would make the partition sum PAST wall and destroy the one
    // property that makes it trustworthy. So the off-thread raster time is
    // printed HERE, beside the partition, explicitly labelled off-thread and
    // explicitly excluded from unaccounted_ns.
    //
    // Reading it: nv2a_raster_witnessed in [perf-excl] measures part 0's share
    // plus the per-job barrier wait, NOT the helpers' work. Total software
    // raster CPU time for the run is part0_raster_ns + worker_raster_ns; at
    // N = 4 that made the bucket under-report the true raster cost by ~3.7x.
    // Printed only under --perf-accounting, because ns_per_tick is only
    // calibrated there (perf_excl::init() is called from the same flag).
    if (xlle::perf_excl::g_enabled) {
        const uint64_t helper_ticks =
            now.span_helper_busy_ticks - prev.span_helper_busy_ticks;
        const uint64_t part0_ticks =
            now.span_part0_busy_ticks - prev.span_part0_busy_ticks;
        const uint64_t barrier_ticks =
            now.span_barrier_wait_ticks - prev.span_barrier_wait_ticks;
        const uint64_t fold_ticks = now.span_fold_ticks - prev.span_fold_ticks;
        // Stage (e) N == 1 keeps its own accumulator; report it in the same
        // line so one parser covers both threaded modes.
        const uint64_t async_ticks =
            now.worker_busy_ticks - prev.worker_busy_ticks;
        const unsigned helpers =
            nv->draw_threads() > 1 ? nv->draw_threads() - 1 : 0;
        std::printf(
            "[perf-worker] scope=%s deadline=%llu off_thread=1 "
            "in_partition=0 threads=%u helpers=%u ns_per_tick=%.6f "
            "worker_raster_ns=%llu async_worker_raster_ns=%llu "
            "part0_raster_ns=%llu barrier_wait_ns=%llu span_fold_ns=%llu "
            "span_jobs=%llu\n",
            scope, deadline, nv->draw_threads(), helpers,
            xlle::perf_excl::g_ns_per_tick,
            (unsigned long long)xlle::perf_excl::ticks_to_ns(helper_ticks),
            (unsigned long long)xlle::perf_excl::ticks_to_ns(async_ticks),
            (unsigned long long)xlle::perf_excl::ticks_to_ns(part0_ticks),
            (unsigned long long)xlle::perf_excl::ticks_to_ns(barrier_ticks),
            (unsigned long long)xlle::perf_excl::ticks_to_ns(fold_ticks),
            (unsigned long long)(now.span_jobs - prev.span_jobs));
    }
    prev = now;
}

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
    if (mach.nv2a() &&
        !mach.nv2a()->debug_resolve_color_surface(
            address, static_cast<size_t>(source_size), "debug surface capture")) {
        error = "resident surface resolve failed";
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

bool dump_guest_memory(XboxMachine& mach, const char* path, uint32_t address,
                       uint64_t size, std::string& error) {
    if (!path || !path[0] || size == 0 || size > 64ull * 1024ull * 1024ull ||
        static_cast<uint64_t>(address) + size > 64ull * 1024ull * 1024ull) {
        error = "memory dump range is empty or exceeds unified RAM";
        return false;
    }
    if (mach.nv2a() &&
        !mach.nv2a()->debug_resolve_color_surface(
            address, static_cast<size_t>(size), "debug memory dump")) {
        error = "resident surface resolve failed";
        return false;
    }
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!mach.read(address, data.data(), data.size())) {
        error = "guest-memory read failed";
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "output open failed";
        return false;
    }
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    if (!out.good()) {
        error = "output write failed";
        return false;
    }
    error.clear();
    return true;
}

bool write_generic_packet_dump(const char* path,
                               const Nv2a::GenericPacketDebugInfo& info,
                               const Nv2a::GenericPacketSnapshot& snapshot,
                               std::string& error) {
    const Nv2aGenericCombinerPacket& packet = snapshot.packet;
    if (!path || !path[0]) {
        error = "empty output path";
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "output open failed";
        return false;
    }
    auto write_bytes = [&](const void* data, size_t size) {
        if (size == 0) return true;
        out.write(reinterpret_cast<const char*>(data),
                  static_cast<std::streamsize>(size));
        return out.good();
    };
    auto write_u8 = [&](uint8_t value) { return write_bytes(&value, sizeof(value)); };
    auto write_u32 = [&](uint32_t value) {
        return write_bytes(&value, sizeof(value));
    };
    auto write_u64 = [&](uint64_t value) {
        return write_bytes(&value, sizeof(value));
    };
    auto write_i32 = [&](int32_t value) {
        return write_bytes(&value, sizeof(value));
    };
    auto write_float = [&](float value) {
        return write_bytes(&value, sizeof(value));
    };
    auto write_vec = [&](const std::vector<uint8_t>& bytes) {
        return write_u64(static_cast<uint64_t>(bytes.size())) &&
               write_bytes(bytes.data(), bytes.size());
    };
    auto write_float4 = [&](const std::array<float, 4>& values) {
        for (float value : values) {
            if (!write_float(value)) return false;
        }
        return true;
    };
    auto write_vertex = [&](const Nv2aGenericCombinerVertex& vertex) {
        if (!write_float4(vertex.position) || !write_float4(vertex.diffuse) ||
            !write_float4(vertex.specular)) {
            return false;
        }
        for (const auto& tex : vertex.texture) {
            if (!write_float4(tex)) return false;
        }
        return true;
    };

    const char magic[8] = {'X', 'L', 'L', 'E', 'G', 'P', 'B', '3'};
    if (!write_bytes(magic, sizeof(magic)) || !write_u32(3) ||
        !write_u64(info.candidate) || !write_u64(info.fingerprint) ||
        !write_u64(info.full_state_hash) || !write_u32(info.color_address) ||
        !write_u32(info.depth_address) || !write_u32(info.pitch) ||
        !write_u32(info.width) || !write_u32(info.height) ||
        !write_u32(info.surface_format) ||
        !write_u32(info.shader_stage_program) ||
        !write_u32(info.combiner_control) ||
        !write_u32(info.texture_enable_mask) ||
        !write_u32(info.primitive_mode) || !write_u32(info.vertex_count)) {
        error = "debug metadata write failed";
        return false;
    }
    if (!write_u8(packet.primitive_mode) ||
        !write_u32(packet.surface_format) ||
        !write_u32(packet.surface_pitch) ||
        !write_u32(packet.surface_height) ||
        !write_u32(packet.color_address) ||
        !write_u32(packet.depth_address) ||
        !write_u8(static_cast<uint8_t>(packet.compute_fixture)) ||
        !write_u32(packet.fixture_clip_x) ||
        !write_u32(packet.fixture_clip_y) ||
        !write_u32(packet.fixture_clip_width) ||
        !write_u32(packet.fixture_clip_height)) {
        error = "packet header write failed";
        return false;
    }
    for (int32_t value : packet.fixture_xy_16) {
        if (!write_i32(value)) {
            error = "fixture xy write failed";
            return false;
        }
    }
    if (!write_u32(packet.fixture_source_argb) ||
        !write_u32(packet.fixture_constant_argb) ||
        !write_u32(packet.fixture_factor0_stage2_argb) ||
        !write_u32(packet.fixture_factor0_stage4_argb) ||
        !write_u32(packet.fixture_blend_src) ||
        !write_u32(packet.fixture_blend_dst) ||
        !write_u32(packet.fixture_depth_func) ||
        !write_u8(packet.fixture_blend_enabled ? 1 : 0) ||
        !write_u8(packet.fixture_depth_test_enabled ? 1 : 0) ||
        !write_u8(packet.fixture_depth_write_enabled ? 1 : 0) ||
        !write_u8(packet.fixture_texture_passthrough ? 1 : 0) ||
        !write_u8(packet.fixture_texture_constant_color_alpha ? 1 : 0) ||
        !write_u8(packet.fixture_color_format_x8 ? 1 : 0) ||
        !write_u8(packet.fixture_alpha_constant_modulate ? 1 : 0) ||
        !write_u8(packet.fixture_alpha_kill_zero ? 1 : 0) ||
        !write_u64(static_cast<uint64_t>(packet.vertices.size()))) {
        error = "fixture fields write failed";
        return false;
    }
    for (const auto& vertex : packet.vertices) {
        if (!write_vertex(vertex)) {
            error = "vertex write failed";
            return false;
        }
    }
    if (!write_u64(static_cast<uint64_t>(packet.raster_state.size())) ||
        !write_bytes(packet.raster_state.data(),
                     packet.raster_state.size() * sizeof(uint32_t))) {
        error = "raster-state write failed";
        return false;
    }
    for (const auto& texture : packet.textures) {
        if (!write_u8(texture.referenced ? 1 : 0) ||
            !write_u8(static_cast<uint8_t>(texture.layout)) ||
            !write_u8(static_cast<uint8_t>(texture.format)) ||
            !write_u8(static_cast<uint8_t>(texture.mode)) ||
            !write_u32(texture.width) || !write_u32(texture.height) ||
            !write_u32(texture.pitch) || !write_u32(texture.address_mode) ||
            !write_u32(texture.border_color) || !write_vec(texture.input)) {
            error = "texture write failed";
            return false;
        }
    }
    if (!write_vec(packet.color_input) || !write_vec(packet.depth_input) ||
        !write_vec(snapshot.color_output) || !write_vec(snapshot.depth_output)) {
        error = "surface snapshot write failed";
        return false;
    }
    if (!out.good()) {
        error = "output write failed";
        return false;
    }
    error.clear();
    return true;
}

}  // namespace

// Idle fast-forward default (beads-xbox.1.4.7). FLIPPED TO TRUE 2026-09-05: the byte-lock
// passed once the copy-on-write HDD attach (beads-xbox.1.14.11) made two runs of the same
// binary the same experiment. Same binary, --nv2a-draw-threads 0, profile hashes on, 12 B,
// 6 legs off vs 3 legs on (build/det/t01..t06 vs build/det/ff1..ff3): identical final frame
// SHA-256, all 24 checkpoint PPMs and the scanout hash chain, all 22 per-profile hash lines,
// retired/IRQ0/injected/pending/early-stop counters, the IDE census, AND the two always-on
// input digests -- dev_digest over all 17,588,661 device-register accesses and dma_digest over
// all 1,610,786 device DMA writes into guest RAM. Only the slice census moves, which is what
// the feature is defined to move. See docs/PERF-METHODOLOGY.md section 3d.
static constexpr bool kIdleFastForwardDefault = true;

// ---- draw-thread and profile-hash defaults (beads-xbox.1.15.9, 2026-09-05) ----
//
// Stage (e2) span-parallel raster is DEFAULT ON. It is work division, not
// latency hiding: each draw's destination scanlines are split across N parts
// that cannot touch each other's pixels, with a barrier per draw, so exactly one
// draw is ever in flight and every coherence anchor sees an empty queue -- the
// same state --nv2a-draw-threads 0 leaves them in. --nv2a-draw-threads 0 remains
// the inline fallback and runs the identical executor with parts == 1.
//
// kDrawThreadsAutoTarget is the part count the knee sweep chose. The effective
// default is min(target, hardware_concurrency - 2) so a smaller host is not
// oversubscribed -- two logical processors are left for the GL driver, the host
// poll and the OS -- and 0 (inline) on anything with fewer than two, where
// ndsrecomp measured a REGRESSION from this class of change.
// 12, from the knee sweep of 2026-09-05 on the reference host (9800X3D, 8C/16T),
// same binary, interleaved 3+3, total 12 B wall, median AND min, 5 % threshold:
//   0 -> 4 -> 8   beads-xbox.1.15.9, PASS at both steps (-26.8 %, -11.8 %)
//   8 -> 12       PASS  median -6.28 %, min -6.14 %, 3 of 3 rounds favour 12
//   12 -> 16      FAIL  median +24.07 %, min +12.37 % (a REGRESSION), and arm 16
//                 has cv 7.3 % against 0.33 % at 12 -- the signature of running
//                 the emulation thread plus 15 helpers on 16 logical processors
//                 with nothing left for the GL driver, the host poll and the OS.
// So the knee is 12 and the rule ("largest N that clears 5 % over the previous
// step, else the previous step") selects it.
static constexpr unsigned kDrawThreadsAutoTarget = 12;

static unsigned auto_draw_threads() {
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw < 2) return 0;
    const unsigned budget = hw - 2;
    unsigned n = kDrawThreadsAutoTarget < budget ? kDrawThreadsAutoTarget : budget;
    if (n > xlle::Nv2a::kDrawThreadsMax) n = xlle::Nv2a::kDrawThreadsMax;
    return n < 2 ? 0 : n;
}

// Per-write FNV-1a profile hashes are DEFAULT OFF. They are an instrument, not
// guest-visible output: the fold is a two-cycle loop-carried dependency executed
// once per written word (727 M writes for the radial profile alone) on the
// hottest loop in the emulator, and -- decisively -- hash_on forces every job
// INLINE (design section 4.3 option O3), so leaving them on would silently
// disable the span raster that is now the default. The exactness surface is
// unchanged in substance: the framebuffer SHA, the 24 frame-series checkpoint
// PPMs, the scanout hash chain and every guest-side counter remain the record.
// --nv2a-profile-hashes turns the folds back on for a hash-level comparison.
// Stage (f) witnessed-profile GPU executor default (beads-xbox.1.15.7).
//
// DEFAULT ON as of 2026-09-05 (beads-xbox.1.15.10 phase 2 session 2). It was
// off while the executor lost the gate: phase 1 measured -30.02 pct median,
// phase 2 session 1 -13.15 pct. Session 2 removed the three mechanisms behind
// that (the GPU serialized behind the resolve, FP64 in the fragment shaders,
// and per-pixel staging on both sides of the transfer) and the 3+3 interleaved
// gate then reads median -6.11 pct / min -5.85 pct FASTER against the 5 pct
// bar, 3 of 3 rounds, with the golden final SHA on every leg and 24 of 28
// checkpoints byte-identical (the other four differ by ONE pixel each, 1-4
// LSB, largest connected component 1 px).
// --no-nv2a-gpu-profiles is the software-raster fallback and runs the identical
// executor with every job declined.
constexpr bool kNv2aGpuProfilesDefault = true;
static constexpr bool kNv2aProfileHashesDefault = false;

int main(int argc, char** argv) {
    bool serve = false, window = false, interactive = false;
    bool test_synthetic_controller_hub = false;
    bool test_synthetic_controller_hub_input_down = false;
    bool test_synthetic_controller_hub_input_down_a = false;
    bool reduced_retire_diag = false;
    // CPU tier (beads-xbox.1.4.6). Default: block-granularity scheduling, no per-instruction
    // hook. --cpu-insn-hook is the toggle-off arm -- identical binary, identical device
    // models, identical guest workload, only the floor's scheduling granularity differs.
    bool cpu_insn_hook = false;
    // Default: the guest time-stamp counter is the retired-instruction clock. The legacy
    // behaviour passed the HOST rdtsc straight through, which makes guest behaviour depend
    // on how fast the emulator runs; keep it only to compare against a pre-2026-09-04 build.
    bool legacy_host_tsc = false;
    // Idle fast-forward (beads-xbox.1.4.7). Retires whole kernel-idle-loop iterations
    // without executing them, bounded by the next device deadline. Default is set by the
    // byte-lock verdict recorded on the bead; both spellings always exist so an A/B can
    // toggle it on the SAME binary.
    bool idle_fast_forward = kIdleFastForwardDefault;
    // HDD write policy (beads-xbox.1.14.11). Default copy-on-write: guest writes are held in
    // memory for the run and the image file is never modified, so consecutive replays start
    // from identical bytes. --hdd-writeback opts back into persisting them.
    bool hdd_writeback = false;
    // Determinism trace reporting (beads-xbox.1.14.11). Capture is always on in
    // XboxMachine; these only choose where it is reported.
    const char* det_log_path = nullptr;
    uint64_t det_log_interval = 1000000;
    const char* det_ring_dump_path = nullptr;
    uint64_t det_ring_dump_at = 0;
    const char* det_dev_dump_path = nullptr;
    uint64_t det_dev_dump_at = 0;
    const char* det_dma_dump_path = nullptr;
    uint64_t det_dma_dump_at = 0;
    std::vector<std::pair<std::string, uint64_t>> det_ram_dumps;
    bool perf_accounting = false;
    bool perf_pull_drain = false;
    // NV2A detailed telemetry used to ride on --perf-accounting (see the set_detailed_
    // telemetry call below). That coupling meant an instrumented run executed DIFFERENT
    // hot-loop code from the run it was supposed to be measuring: collect_diagnostics
    // turns on per-vertex stats folding and the generic-combiner fingerprint census
    // (nv2a.cpp submit_vertex_batch / rasterize_generic_combiner_batch), which is a large
    // part of the 17.1 % cost attributed to --perf-accounting
    // (docs/PERF-AUDIT-2026-09-02.md §6). It now has its own switch
    // (beads-xbox.1.14.3 item 4).
    bool nv2a_detailed_telemetry = false;
    // Per-write FNV-1a output-hash folds in the witnessed raster fragment loop
    // (beads-xbox.1.15.2 stage b2). Default OFF as of 2026-09-05 -- see
    // kNv2aProfileHashesDefault above for why. --nv2a-profile-hashes turns them
    // back on and, by option O3, forces every draw inline with them.
    bool nv2a_profile_hashes = kNv2aProfileHashesDefault;
    // Stage (f): the witnessed-profile GPU executor. Default OFF until the
    // behavioral comparison in PERF-METHODOLOGY.md section 3 passes.
    bool nv2a_gpu_profiles = kNv2aGpuProfilesDefault;
    // Default ON as of beads-xbox.1.15.10 phase 6: two independent 3+3
    // interleaved sets on one binary and one session gave +6.63 / +6.46 pct and
    // +6.81 / +6.79 pct on the total 12 B wall (median / min), 6 of 6 rounds,
    // against a 5 pct bar, with the golden final SHA reproduced on every leg.
    // --no-nv2a-gpu-generic-shader restores the previous behaviour on the SAME
    // binary, so the decision is a one-flag change either way.
    bool nv2a_gpu_generic_shader = true;
    uint64_t nv2a_gpu_generic_shader_mask = ~uint64_t{0};
    bool nv2a_gpu_generic_shader_fp64 = true;
    bool nv2a_gpu_generic_shader_image_blend = true;
    bool nv2a_gpu_generic_shader_nodraw = false;
    uint32_t nv2a_gpu_generic_shader_wide = 4096;
    uint32_t nv2a_gpu_generic_shader_verify = 0;
    // Stage (f): let the GPU executor take jobs whose 1/16-pixel screen
    // coordinates exceed the exact-double product bound. Off by default: it is
    // faster but it moved one checkpoint frame (nv2a.hpp
    // kGpuFixedCoordinateLimitExact).
    bool nv2a_gpu_relaxed_coords = false;
    bool nv2a_gpu_exact_coords = false;
    // Bit 0 = kSwizzledRadialColor, bit 1 = kPitchTexturedColor,
    // bit 2 = kPitchProjectedColor (beads-xbox.1.15.10 phase 4, lever A).
    uint32_t nv2a_gpu_profile_mask = 7;
    bool nv2a_gpu_vblank_resolve = false;
    // beads-xbox.1.15.10 step 1 (overlap). The phase-2 measurement had
    // 5 628 312 triangles in 965 draw calls (5 832 each, ~8.0 ms of GPU work
    // each), every one issued immediately before the readback that waited on
    // it. 0 restores that unbounded batch. The default is the flat part of a
    // measured 2 B sweep (total wall, two rounds, same session): 32 -> 15.01 s,
    // 64 -> 15.07, 128 -> 14.98, 256 -> 15.09, 1024 -> 15.72, 4096 -> 16.24,
    // unbounded -> 17.82. 256 is the largest bound still on the floor, so it
    // takes the floor with the fewest driver submissions.
    uint64_t nv2a_gpu_flush_triangles = 256;
    bool nv2a_gpu_vblank_kick = true;
    // beads-xbox.1.15.10 phase 3 step 3: serve a whole-plane CLEAR_SURFACE on a
    // GPU-authoritative pair as a glClear instead of a resolve + software fill
    // + invalidate. Default ON with the executor; --no-nv2a-gpu-clear restores
    // the software clear on the same binary so the two can be A/B'd.
    bool nv2a_gpu_clear = true;
    bool nv2a_gpu_skip_empty_packets = true;
    bool nv2a_gpu_kick_flush = true;
    // beads-xbox.1.15.10 phase 4 (lever C). --no-nv2a-gpu-plane-split is
    // the A/B arm that restores the pre-phase-4 pair-scoped slot rule.
    bool nv2a_gpu_plane_split = true;
    // beads-xbox.1.15.10 phase 4 (lever B) CEILING PROBE. DIAGNOSTIC, KNOWN
    // INCORRECT when on: it skips the resolve that keeps a witnessed texture
    // snapshot coherent with a GPU-authoritative surface. Its only purpose is
    // to bound what a correct resident-texture sampler could win.
    bool nv2a_gpu_unsafe_skip_texture_resolve = false;
    // beads-xbox.1.15.10 phase 5 item 3. The parsed vertex-program cache.
    // DEFAULT ON; --no-nv2a-vertex-program-cache restores the per-batch
    // nv2a_vsh_parse_program call verbatim so the change is a same-binary A/B.
    bool nv2a_vertex_program_cache = true;
    // Phase 5 item 3 attribution instrument. DEFAULT OFF: it takes two clock
    // reads per VERTEX, which is tens of millions per replay. Never a shipping
    // path; it exists so the vertex split is measured rather than inferred.
    bool nv2a_vertex_census = false;
    // Phase 5 item 3. The two exactness-neutral per-vertex hoists. DEFAULT ON;
    // --no-nv2a-vertex-fast-fetch restores the previous loop verbatim.
    bool nv2a_vertex_fast_fetch = true;
    // beads-xbox.1.15.11. The pre-decoded vertex-shader step form. DEFAULT ON;
    // --no-nv2a-vsh-compile runs the vendored nv2a_vsh_emu_execute on the same
    // binary, which is the arm the decision A/B is measured against.
    bool nv2a_vsh_compile = true;
    // Ceiling probe (beads-xbox.1.15.11). DEFAULT 0 and no --no- spelling,
    // because it is knowingly incorrect when armed: 1 skips the operation and
    // stores the first operand, 2 skips the stores too. It bounds how much of
    // the per-op cost is the vendored call rather than the operand traffic.
    uint32_t nv2a_vsh_unsafe_probe = 0;
    // The differential verifier (beads-xbox.1.15.11). DIAGNOSTIC, default 0,
    // very slow when armed: it runs the vendored interpreter a SECOND time for
    // each checked vertex and compares the seven output registers the device
    // reads back, bit for bit.
    uint32_t nv2a_vsh_verify = 0;
    uint32_t nv2a_gpu_ssbo_ring = 4;
    // beads-xbox.1.15.10 step 2. DEFAULT ON with the executor: FP64 in the
    // witnessed fragment shaders cost gpu_draw_ns 10.746 s against 2.364 s for
    // the same run in single precision (matched instrumented pair, 12 B), and
    // it drags drain_ns with it (3.715 -> 0.889 s). The 28-checkpoint
    // comparison against the golden is the acceptance test and it passes.
    // --no-nv2a-gpu-fp32-shader restores the double-precision variant as a
    // bisection tool; the SSBO record is byte-identical either way.
    bool nv2a_gpu_fp32_shader = true;
    // Draw threads. 0 = inline, the toggle-off fallback, which runs the SAME
    // executor with parts == 1; 1 = the stage (e) asynchronous single consumer
    // and its ring, behind the ten coherence anchors of DESIGN-NV2A-SEAM.md
    // section 2.2(c), retained as a mechanism and never a win; N >= 2 = stage
    // (e2) span-parallel work division (beads-xbox.1.15.9), which is the
    // DEFAULT as of 2026-09-05 -- see auto_draw_threads() above. A job is only
    // divisible with the per-write FNV folds off, which is why those now default
    // off too; --nv2a-profile-hashes forces every draw inline again.
    unsigned nv2a_draw_threads = auto_draw_threads();
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
    const char* debug_memory_dump = nullptr;
    const char* debug_generic_surface_snapshot_prefix = nullptr;
    const char* debug_generic_packet_dump = nullptr;
    const char* debug_nv2a_surface_snapshot_prefix = nullptr;
    bool debug_nv2a_surface_events = false;
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
    uint32_t debug_memory_address = 0;
    uint64_t debug_memory_size = 0;
    uint32_t debug_generic_surface_snapshot_address = 0;
    uint64_t debug_generic_surface_snapshot_skip = 0;
    uint64_t debug_generic_surface_snapshot_count = 0;
    uint64_t debug_generic_surface_snapshot_interval = 1;
    uint64_t debug_generic_packet_dump_fingerprint = 0;
    uint64_t debug_generic_packet_dump_full_hash = 0;
    uint64_t debug_generic_packet_dump_skip = 0;
    uint32_t debug_nv2a_surface_snapshot_address = 0;
    uint64_t debug_nv2a_surface_snapshot_skip = 0;
    uint64_t debug_nv2a_surface_snapshot_count = 0;
    uint64_t debug_nv2a_surface_snapshot_interval = 1;
    uint64_t debug_nv2a_surface_snapshot_min_writes = 0;
    uint32_t debug_nv2a_surface_event_address = 0;
    uint64_t debug_nv2a_surface_event_skip = 0;
    uint64_t debug_nv2a_surface_event_count = 0;
    uint64_t debug_nv2a_surface_event_interval = 1;
    uint64_t debug_nv2a_surface_event_after_insns = 0;
    size_t perf_generic_top_limit = 12;
    uint32_t host_sample_profile_ms = 0;
    uint64_t max_insns = 128ull * 1000 * 1000;  // 2BL kernel LZX decompress needs tens of M
    uint64_t machine_chunk_insns = 4096;
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
        else if (!std::strcmp(argv[i], "--cpu-insn-hook"))
            cpu_insn_hook = true;
        else if (!std::strcmp(argv[i], "--legacy-host-tsc"))
            legacy_host_tsc = true;
        else if (!std::strcmp(argv[i], "--det-log") && i + 2 < argc) {
            det_log_path = argv[++i];
            det_log_interval = std::strtoull(argv[++i], nullptr, 0);
            if (det_log_interval == 0) {
                std::fprintf(stderr,
                             "[host] --det-log needs a positive interval\n");
                return 1;
            }
        }
        else if (!std::strcmp(argv[i], "--det-ring-dump") && i + 2 < argc) {
            det_ring_dump_path = argv[++i];
            det_ring_dump_at = std::strtoull(argv[++i], nullptr, 0);
        }
        else if (!std::strcmp(argv[i], "--det-dev-dump") && i + 2 < argc) {
            det_dev_dump_path = argv[++i];
            det_dev_dump_at = std::strtoull(argv[++i], nullptr, 0);
        }
        else if (!std::strcmp(argv[i], "--det-dma-dump") && i + 2 < argc) {
            det_dma_dump_path = argv[++i];
            det_dma_dump_at = std::strtoull(argv[++i], nullptr, 0);
        }
        else if (!std::strcmp(argv[i], "--det-ram-dump") && i + 2 < argc) {
            const char* path = argv[++i];
            det_ram_dumps.emplace_back(path, std::strtoull(argv[++i], nullptr, 0));
        }
        else if (!std::strcmp(argv[i], "--hdd-writeback"))
            hdd_writeback = true;
        else if (!std::strcmp(argv[i], "--no-hdd-writeback"))
            hdd_writeback = false;
        else if (!std::strcmp(argv[i], "--idle-fast-forward"))
            idle_fast_forward = true;
        else if (!std::strcmp(argv[i], "--no-idle-fast-forward"))
            idle_fast_forward = false;
        else if (!std::strcmp(argv[i], "--perf-accounting"))
            perf_accounting = true;
        else if (!std::strcmp(argv[i], "--perf-pull-drain"))
            perf_pull_drain = true;
        else if (!std::strcmp(argv[i], "--nv2a-detailed-telemetry"))
            nv2a_detailed_telemetry = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-profile-hashes"))
            nv2a_profile_hashes = false;
        // Positive spelling. BOTH spellings always exist on the same binary so
        // the instrument can be toggled in an A/B without changing the build,
        // whichever way the default points (PERF-METHODOLOGY.md section 3).
        else if (!std::strcmp(argv[i], "--nv2a-profile-hashes"))
            nv2a_profile_hashes = true;
        // Stage (f) GPU-resident consumer (beads-xbox.1.15.7). Both spellings
        // always exist so the executor can be toggled in a same-binary A/B
        // whichever way the default points (PERF-METHODOLOGY.md section 3).
        else if (!std::strcmp(argv[i], "--nv2a-gpu-profiles"))
            nv2a_gpu_profiles = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-gpu-profiles"))
            nv2a_gpu_profiles = false;
        // Stage (f3) the GENERATED generic-combiner executor
        // (beads-xbox.1.15.10 phase 6). Both spellings always exist so the
        // decision A/B is a one-flag change on one binary.
        else if (!std::strcmp(argv[i], "--nv2a-gpu-generic-shader"))
            nv2a_gpu_generic_shader = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-gpu-generic-shader"))
            nv2a_gpu_generic_shader = false;
        else if (!std::strcmp(argv[i], "--nv2a-gpu-generic-shader-nodraw"))
            nv2a_gpu_generic_shader_nodraw = true;
        else if (!std::strcmp(argv[i], "--nv2a-gpu-generic-shader-verify") &&
                 i + 1 < argc) {
            uint64_t parsed = 0;
            if (!parse_u64_arg(argv[++i], parsed) || parsed > 100000ull) {
                std::fprintf(stderr,
                             "invalid --nv2a-gpu-generic-shader-verify\n");
                return 2;
            }
            nv2a_gpu_generic_shader_verify = static_cast<uint32_t>(parsed);
        }
        else if (!std::strcmp(argv[i], "--nv2a-gpu-generic-shader-wide") &&
                 i + 1 < argc) {
            uint64_t parsed = 0;
            if (!parse_u64_arg(argv[++i], parsed) || parsed > 0x0FFFFFFFull) {
                std::fprintf(stderr,
                             "invalid --nv2a-gpu-generic-shader-wide\n");
                return 2;
            }
            nv2a_gpu_generic_shader_wide = static_cast<uint32_t>(parsed);
        }
        else if (!std::strcmp(argv[i], "--nv2a-gpu-generic-shader-image-blend"))
            nv2a_gpu_generic_shader_image_blend = true;
        else if (!std::strcmp(argv[i],
                              "--no-nv2a-gpu-generic-shader-image-blend"))
            nv2a_gpu_generic_shader_image_blend = false;
        else if (!std::strcmp(argv[i], "--nv2a-gpu-generic-shader-fp64"))
            nv2a_gpu_generic_shader_fp64 = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-gpu-generic-shader-fp64"))
            nv2a_gpu_generic_shader_fp64 = false;
        else if (!std::strcmp(argv[i], "--nv2a-gpu-generic-shader-mask") &&
                 i + 1 < argc) {
            uint64_t parsed = 0;
            if (!parse_u64_arg(argv[++i], parsed)) {
                std::fprintf(stderr, "invalid --nv2a-gpu-generic-shader-mask\n");
                return 2;
            }
            nv2a_gpu_generic_shader_mask = parsed;
        }
        else if (!std::strcmp(argv[i], "--nv2a-gpu-profile-mask") && i + 1 < argc) {
            uint64_t parsed = 0;
            if (!parse_u64_arg(argv[++i], parsed) || parsed > 7) {
                std::fprintf(stderr, "invalid --nv2a-gpu-profile-mask\n");
                return 2;
            }
            nv2a_gpu_profile_mask = static_cast<uint32_t>(parsed);
        }
        else if (!std::strcmp(argv[i], "--nv2a-gpu-vblank-resolve"))
            nv2a_gpu_vblank_resolve = true;
        // beads-xbox.1.15.10 step 1 (overlap): triangle bound on the pending
        // witnessed batch. 0 = unbounded, the pre-step-1 behavior where the
        // batch is issued only when a resolve needs it and no GPU work overlaps
        // the emulation thread. Both spellings of the vblank kick exist on one
        // binary so either can be bisected without a rebuild.
        else if (!std::strcmp(argv[i], "--nv2a-gpu-flush-triangles") &&
                 i + 1 < argc) {
            uint64_t parsed = 0;
            if (!parse_u64_arg(argv[++i], parsed)) {
                std::fprintf(stderr, "invalid --nv2a-gpu-flush-triangles\n");
                return 2;
            }
            nv2a_gpu_flush_triangles = parsed;
        }
        else if (!std::strcmp(argv[i], "--nv2a-gpu-vblank-kick"))
            nv2a_gpu_vblank_kick = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-gpu-vblank-kick"))
            nv2a_gpu_vblank_kick = false;
        else if (!std::strcmp(argv[i], "--nv2a-gpu-clear"))
            nv2a_gpu_clear = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-gpu-clear"))
            nv2a_gpu_clear = false;
        else if (!std::strcmp(argv[i], "--nv2a-gpu-skip-empty-packets"))
            nv2a_gpu_skip_empty_packets = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-gpu-skip-empty-packets"))
            nv2a_gpu_skip_empty_packets = false;
        // beads-xbox.1.15.10 step 2: single-precision witnessed fragment
        // shaders. Both spellings on one binary; the record layout is identical
        // either way, so this is a same-binary A/B.
        else if (!std::strcmp(argv[i], "--nv2a-gpu-fp32-shader"))
            nv2a_gpu_fp32_shader = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-gpu-fp32-shader"))
            nv2a_gpu_fp32_shader = false;
        else if (!std::strcmp(argv[i], "--nv2a-gpu-kick-flush"))
            nv2a_gpu_kick_flush = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-gpu-kick-flush"))
            nv2a_gpu_kick_flush = false;
        else if (!std::strcmp(argv[i], "--nv2a-gpu-unsafe-skip-texture-resolve"))
            nv2a_gpu_unsafe_skip_texture_resolve = true;
        else if (!std::strcmp(argv[i], "--nv2a-vertex-program-cache"))
            nv2a_vertex_program_cache = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-vertex-program-cache"))
            nv2a_vertex_program_cache = false;
        else if (!std::strcmp(argv[i], "--nv2a-vertex-census"))
            nv2a_vertex_census = true;
        else if (!std::strcmp(argv[i], "--nv2a-vertex-fast-fetch"))
            nv2a_vertex_fast_fetch = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-vertex-fast-fetch"))
            nv2a_vertex_fast_fetch = false;
        else if (!std::strcmp(argv[i], "--nv2a-vsh-compile"))
            nv2a_vsh_compile = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-vsh-compile"))
            nv2a_vsh_compile = false;
        else if (!std::strcmp(argv[i], "--nv2a-vsh-unsafe-probe") && i + 1 < argc)
            nv2a_vsh_unsafe_probe =
                static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        else if (!std::strcmp(argv[i], "--nv2a-vsh-verify") && i + 1 < argc)
            nv2a_vsh_verify =
                static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        else if (!std::strcmp(argv[i], "--nv2a-gpu-plane-split"))
            nv2a_gpu_plane_split = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-gpu-plane-split"))
            nv2a_gpu_plane_split = false;
        else if (!std::strcmp(argv[i], "--nv2a-gpu-ssbo-ring") && i + 1 < argc) {
            uint64_t parsed = 0;
            if (!parse_u64_arg(argv[++i], parsed) || parsed < 1 || parsed > 16) {
                std::fprintf(stderr, "invalid --nv2a-gpu-ssbo-ring (1..16)\n");
                return 2;
            }
            nv2a_gpu_ssbo_ring = static_cast<uint32_t>(parsed);
        }
        else if (!std::strcmp(argv[i], "--nv2a-gpu-relaxed-coords"))
            nv2a_gpu_relaxed_coords = true;
        // Phase 2 (beads-xbox.1.15.10): the relaxed bound is now the default;
        // this restores the pre-phase-2 2^24 bound as a bisection tool.
        else if (!std::strcmp(argv[i], "--nv2a-gpu-exact-coords"))
            nv2a_gpu_exact_coords = true;
        else if (!std::strcmp(argv[i], "--no-nv2a-gpu-relaxed-coords"))
            nv2a_gpu_relaxed_coords = false;
        else if (!std::strcmp(argv[i], "--nv2a-draw-threads") && i + 1 < argc) {
            uint64_t parsed = 0;
            if (!parse_u64_arg(argv[++i], parsed) ||
                parsed > xlle::Nv2a::kDrawThreadsMax) {
                std::fprintf(stderr,
                             "invalid --nv2a-draw-threads '%s' (0..%u; 0=inline, "
                             "1=stage (e) single async consumer, N>=2=stage (e2) "
                             "destination-span parallel raster, "
                             "beads-xbox.1.15.9)\n",
                             argv[i], xlle::Nv2a::kDrawThreadsMax);
                return 2;
            }
            nv2a_draw_threads = static_cast<unsigned>(parsed);
        }
        else if (!std::strcmp(argv[i], "--perf-generic-top") && i + 1 < argc) {
            uint64_t parsed = 0;
            if (!parse_u64_arg(argv[++i], parsed) || parsed == 0 ||
                parsed > 1000) {
                std::fprintf(
                    stderr,
                    "[host] --perf-generic-top needs an integer in 1..1000\n");
                return 1;
            }
            perf_generic_top_limit = static_cast<size_t>(parsed);
        }
        else if (!std::strcmp(argv[i], "--host-sample-profile") && i + 1 < argc) {
            uint64_t parsed = 0;
            if (!parse_u64_arg(argv[++i], parsed) || parsed == 0 ||
                parsed > 1000) {
                std::fprintf(
                    stderr,
                    "[host] --host-sample-profile needs a period in 1..1000 ms\n");
                return 1;
            }
            host_sample_profile_ms = static_cast<uint32_t>(parsed);
        }
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
        else if (!std::strcmp(argv[i], "--debug-memory-dump")) {
            if (i + 3 >= argc) {
                std::fprintf(
                    stderr,
                    "[host] use --debug-memory-dump <path> <addr> <size>\n");
                return 1;
            }
            debug_memory_dump = argv[++i];
            if (!debug_memory_dump[0] ||
                !parse_u32_arg(argv[++i], debug_memory_address) ||
                !parse_u64_arg(argv[++i], debug_memory_size) ||
                debug_memory_size == 0 ||
                static_cast<uint64_t>(debug_memory_address) +
                        debug_memory_size >
                    64ull * 1024ull * 1024ull) {
                std::fprintf(
                    stderr,
                    "[host] --debug-memory-dump expects path, 32-bit addr, and positive in-RAM size\n");
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
        else if (!std::strcmp(argv[i], "--debug-generic-packet-dump")) {
            if (i + 4 >= argc) {
                std::fprintf(
                    stderr,
                    "[host] use --debug-generic-packet-dump <path> <fingerprint-or-0> <full-hash-or-0> <skip>\n");
                return 1;
            }
            debug_generic_packet_dump = argv[++i];
            if (!debug_generic_packet_dump[0] ||
                !parse_u64_arg(argv[++i],
                               debug_generic_packet_dump_fingerprint) ||
                !parse_u64_arg(argv[++i],
                               debug_generic_packet_dump_full_hash) ||
                !parse_u64_arg(argv[++i], debug_generic_packet_dump_skip)) {
                std::fprintf(
                    stderr,
                    "[host] --debug-generic-packet-dump expects path, two 64-bit hash filters (0=wildcard), and skip count\n");
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
        else if (!std::strcmp(argv[i], "--debug-nv2a-surface-snapshots-min-writes") &&
                 i + 1 < argc) {
            if (!parse_u64_arg(argv[++i],
                               debug_nv2a_surface_snapshot_min_writes)) {
                std::fprintf(
                    stderr,
                    "[host] --debug-nv2a-surface-snapshots-min-writes expects "
                    "an unsigned write count\n");
                return 1;
            }
        }
        else if (!std::strcmp(argv[i], "--debug-nv2a-surface-events")) {
            if (i + 4 >= argc) {
                std::fprintf(
                    stderr,
                    "[host] use --debug-nv2a-surface-events <addr> <skip> <count> <interval>\n");
                return 1;
            }
            debug_nv2a_surface_events = true;
            if (!parse_u32_arg(argv[++i], debug_nv2a_surface_event_address) ||
                !parse_u64_arg(argv[++i], debug_nv2a_surface_event_skip) ||
                !parse_u64_arg(argv[++i], debug_nv2a_surface_event_count) ||
                !parse_u64_arg(argv[++i], debug_nv2a_surface_event_interval) ||
                debug_nv2a_surface_event_count == 0 ||
                debug_nv2a_surface_event_interval == 0) {
                std::fprintf(
                    stderr,
                    "[host] --debug-nv2a-surface-events expects 32-bit addr and positive count/interval\n");
                return 1;
            }
        }
        else if (!std::strcmp(argv[i], "--debug-nv2a-surface-events-after-insns") &&
                 i + 1 < argc) {
            if (!parse_u64_arg(argv[++i], debug_nv2a_surface_event_after_insns)) {
                std::fprintf(
                    stderr,
                    "[host] --debug-nv2a-surface-events-after-insns expects "
                    "an unsigned instruction count\n");
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
        else if (!std::strcmp(argv[i], "--machine-chunk-insns") &&
                 i + 1 < argc) {
            if (!parse_u64_arg(argv[++i], machine_chunk_insns) ||
                machine_chunk_insns == 0) {
                std::fprintf(
                    stderr,
                    "[host] --machine-chunk-insns needs a positive integer\n");
                return 1;
            }
        }
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
    if (nv2a_renderer) {
        nv2a_renderer->set_witnessed_flush_triangles(nv2a_gpu_flush_triangles);
        nv2a_renderer->set_witnessed_kick_flush(nv2a_gpu_kick_flush);
        nv2a_renderer->set_witnessed_plane_split(nv2a_gpu_plane_split);
        nv2a_renderer->set_witnessed_ssbo_ring(nv2a_gpu_ssbo_ring);
        std::string fp32_error;
        if (!nv2a_renderer->set_witnessed_fp32_shader(nv2a_gpu_fp32_shader,
                                                      fp32_error)) {
            std::fprintf(stderr, "[nv2a-renderer] %s\n", fp32_error.c_str());
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
        machine_config.cpu_chunk_insns = machine_chunk_insns;
        if (test_synthetic_controller_hub) {
            machine_config.ohci0_topology =
                Ohci::UsbTopology::kSyntheticControllerSHubForTest;
        }
        if (machine_chunk_insns != 4096) {
            std::printf(
                "[machine] cpu chunk override: %llu instructions (default 4096)\n",
                static_cast<unsigned long long>(machine_chunk_insns));
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
            // Idle fast-forward (beads-xbox.1.4.7). Off unless asked for; when on, the skip
            // announces itself so no log can be read as a fast-forwarded run by accident.
            mach.set_idle_fast_forward(idle_fast_forward);
            // Determinism trace sinks (beads-xbox.1.14.11). The per-slice fold and the
            // 1 Mi-slice ring run unconditionally; these only open report sinks.
            if (det_log_path) {
                if (!mach.open_det_log(det_log_path, det_log_interval)) {
                    std::fprintf(stderr, "[host] --det-log: cannot open %s\n", det_log_path);
                    return 1;
                }
                std::printf("[det] checkpoint log %s every %llu instructions\n",
                            det_log_path,
                            static_cast<unsigned long long>(det_log_interval));
            }
            for (const auto& dump : det_ram_dumps) {
                mach.add_det_ram_dump(dump.first, dump.second);
                std::printf("[det] ram dump %s at insns>=%llu\n", dump.first.c_str(),
                            static_cast<unsigned long long>(dump.second));
            }
            if (det_dev_dump_path)
                mach.set_det_dev_dump(det_dev_dump_path, det_dev_dump_at);
            if (det_dma_dump_path)
                mach.set_det_dma_dump(det_dma_dump_path, det_dma_dump_at);
            if (xlle_heap_poison_enabled()) {
                std::printf("[det] heap poison ON: every operator-new block starts as "
                            "0x%02x (beads-xbox.1.14.11)\n", xlle_heap_poison_byte());
            }
            if (det_ring_dump_path) {
                mach.set_det_ring_dump(det_ring_dump_path, det_ring_dump_at);
                std::printf("[det] ring dump %s at insns>=%llu\n", det_ring_dump_path,
                            static_cast<unsigned long long>(det_ring_dump_at));
            }
            if (idle_fast_forward) {
                std::printf("[host] idle-fast-forward ON: whole idle-loop iterations at "
                            "0x%08x are retired without executing, bounded by the next "
                            "device deadline\n", 0x8001e3ceu);
            }
            if (mach.nv2a()) {
                // Decoupled from --perf-accounting (beads-xbox.1.14.3 item 4): timing must
                // not change the code being timed. Use --nv2a-detailed-telemetry to force
                // it on alongside --reduced-retire-diag.
                mach.nv2a()->set_detailed_telemetry(
                    !reduced_retire_diag || nv2a_detailed_telemetry);
                // Default OFF as of 2026-09-05 (kNv2aProfileHashesDefault):
                // the per-write folds are an instrument, and leaving them on
                // would force every draw inline and silently disable the span
                // raster. --nv2a-profile-hashes opts back in.
                mach.nv2a()->set_profile_hashes_enabled(nv2a_profile_hashes);
                mach.nv2a()->set_vertex_program_cache_enabled(
                    nv2a_vertex_program_cache);
                mach.nv2a()->set_vertex_census_enabled(nv2a_vertex_census);
                mach.nv2a()->set_vertex_fast_fetch_enabled(
                    nv2a_vertex_fast_fetch);
                mach.nv2a()->set_vsh_compile_enabled(nv2a_vsh_compile);
                mach.nv2a()->set_vsh_unsafe_probe(nv2a_vsh_unsafe_probe);
                mach.nv2a()->set_vsh_verify(nv2a_vsh_verify);
                mach.nv2a()->set_gpu_profiles_enabled(nv2a_gpu_profiles);
                mach.nv2a()->set_gpu_generic_shader(nv2a_gpu_generic_shader);
                mach.nv2a()->set_gpu_generic_shader_mask(
                    nv2a_gpu_generic_shader_mask);
                mach.nv2a()->set_gpu_generic_shader_fp64(
                    nv2a_gpu_generic_shader_fp64);
                mach.nv2a()->set_gpu_generic_shader_image_blend(
                    nv2a_gpu_generic_shader_image_blend);
                mach.nv2a()->set_gpu_generic_shader_nodraw(
                    nv2a_gpu_generic_shader_nodraw);
                mach.nv2a()->set_gpu_generic_shader_wide_pixels(
                    nv2a_gpu_generic_shader_wide);
                mach.nv2a()->set_gpu_generic_shader_verify(
                    nv2a_gpu_generic_shader_verify);
                mach.nv2a()->set_gpu_relaxed_coords(nv2a_gpu_relaxed_coords);
                mach.nv2a()->set_gpu_exact_coords(nv2a_gpu_exact_coords);
                mach.nv2a()->set_gpu_profile_mask(nv2a_gpu_profile_mask);
                mach.nv2a()->set_gpu_unsafe_skip_texture_resolve(
                    nv2a_gpu_unsafe_skip_texture_resolve);
                mach.nv2a()->set_gpu_vblank_resolve(nv2a_gpu_vblank_resolve);
                mach.nv2a()->set_gpu_vblank_kick(nv2a_gpu_vblank_kick);
                mach.nv2a()->set_gpu_clear_enabled(nv2a_gpu_clear);
                mach.nv2a()->set_gpu_skip_empty_packets(
                    nv2a_gpu_skip_empty_packets);
                // Draw threads. Default is auto_draw_threads() as of
                // 2026-09-05; --nv2a-draw-threads 0 is the inline fallback. The
                // pool is created here and torn down by ~Nv2a. At N >= 2 no job
                // ever enters the ring, so every coherence anchor sees an empty
                // queue -- the state the inline path leaves them in.
                if (!mach.nv2a()->set_draw_threads(nv2a_draw_threads)) {
                    std::fprintf(stderr,
                                 "NV2A draw threads %u refused (max %u)\n",
                                 nv2a_draw_threads, xlle::Nv2a::kDrawThreadsMax);
                    return 2;
                }
                // Printed unconditionally now that threads are on by default:
                // "which mode did this leg actually run in" is part of every
                // report's identity, and the inline case is the one a reader is
                // most likely to assume rather than check.
                std::printf("[host] nv2a-draw-threads=%u hw_concurrency=%u mode=%s "
                            "queue-slots=%zu profile-hashes=%d\n",
                            nv2a_draw_threads,
                            std::thread::hardware_concurrency(),
                            nv2a_draw_threads == 0   ? "inline"
                            : nv2a_draw_threads == 1 ? "async-consumer"
                                                     : "destination-spans",
                            (size_t)xlle::Nv2a::kDrawQueueSlots,
                            nv2a_profile_hashes ? 1 : 0);
            }
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
            if (debug_generic_packet_dump && mach.nv2a()) {
                auto matched_packets = std::make_shared<uint64_t>(0);
                auto wrote_packet = std::make_shared<bool>(false);
                mach.nv2a()->on_generic_packet_snapshot(
                    [=](const Nv2a::GenericPacketDebugInfo& info) mutable {
                        if (*wrote_packet) return false;
                        if (debug_generic_packet_dump_fingerprint != 0 &&
                            info.fingerprint !=
                                debug_generic_packet_dump_fingerprint) {
                            return false;
                        }
                        if (debug_generic_packet_dump_full_hash != 0 &&
                            info.full_state_hash !=
                                debug_generic_packet_dump_full_hash) {
                            return false;
                        }
                        ++(*matched_packets);
                        return *matched_packets >
                               debug_generic_packet_dump_skip;
                    },
                    [=](const Nv2a::GenericPacketDebugInfo& info,
                        const Nv2a::GenericPacketSnapshot& snapshot) mutable {
                        const Nv2aGenericCombinerPacket& packet = snapshot.packet;
                        std::string error;
                        const bool ok = write_generic_packet_dump(
                            debug_generic_packet_dump, info, snapshot, error);
                        if (ok) {
                            *wrote_packet = true;
                            std::printf(
                                "[debug-generic-packet] wrote %s candidate=%llu "
                                "match=%llu fp=%016llx full=%016llx target=%08x "
                                "prim=%u verts=%u sfmt=%08x shader=%08x "
                                "comb=%08x textures=%u color_in=%llu "
                                "depth_in=%llu color_out=%llu depth_out=%llu\n",
                                debug_generic_packet_dump,
                                static_cast<unsigned long long>(info.candidate),
                                static_cast<unsigned long long>(*matched_packets),
                                static_cast<unsigned long long>(info.fingerprint),
                                static_cast<unsigned long long>(
                                    info.full_state_hash),
                                info.color_address, info.primitive_mode,
                                info.vertex_count, info.surface_format,
                                info.shader_stage_program, info.combiner_control,
                                static_cast<unsigned>(
                                    std::count_if(
                                        packet.textures.begin(),
                                        packet.textures.end(),
                                        [](const auto& texture) {
                                            return texture.referenced;
                                        })),
                                static_cast<unsigned long long>(
                                    packet.color_input.size()),
                                static_cast<unsigned long long>(
                                    packet.depth_input.size()),
                                static_cast<unsigned long long>(
                                    snapshot.color_output.size()),
                                static_cast<unsigned long long>(
                                    snapshot.depth_output.size()));
                        } else {
                            std::printf(
                                "[debug-generic-packet] FAILED %s candidate=%llu "
                                "fp=%016llx full=%016llx%s%s%s\n",
                                debug_generic_packet_dump,
                                static_cast<unsigned long long>(info.candidate),
                                static_cast<unsigned long long>(info.fingerprint),
                                static_cast<unsigned long long>(
                                    info.full_state_hash),
                                error.empty() ? "" : " (",
                                error.empty() ? "" : error.c_str(),
                                error.empty() ? "" : ")");
                        }
                        return ok;
                    });
                std::printf(
                    "[host] generic packet dump armed: path=%s fp=%016llx "
                    "full=%016llx skip=%llu\n",
                    debug_generic_packet_dump,
                    static_cast<unsigned long long>(
                        debug_generic_packet_dump_fingerprint),
                    static_cast<unsigned long long>(
                        debug_generic_packet_dump_full_hash),
                    static_cast<unsigned long long>(
                        debug_generic_packet_dump_skip));
            }
            if ((debug_nv2a_surface_snapshot_prefix || debug_nv2a_surface_events) &&
                mach.nv2a()) {
                uint64_t matched_events = 0;
                uint64_t written_snapshots = 0;
                uint64_t logged_events = 0;
                bool snapshot_failure_logged = false;
                mach.nv2a()->on_surface_write_complete(
                    [&, matched_events, written_snapshots, logged_events,
                     snapshot_failure_logged](const Nv2a::SurfaceWriteDebugInfo& info)
                        mutable {
                            const bool snapshot_target =
                                debug_nv2a_surface_snapshot_prefix &&
                                info.color_address ==
                                    debug_nv2a_surface_snapshot_address;
                            const bool event_target =
                                debug_nv2a_surface_events &&
                                info.color_address ==
                                    debug_nv2a_surface_event_address;
                            if (!snapshot_target && !event_target)
                                return;
                            if (event_target &&
                                mach.total_insns() <
                                    debug_nv2a_surface_event_after_insns)
                                return;
                            ++matched_events;
                            if (event_target) {
                                const bool event_after_skip =
                                    matched_events > debug_nv2a_surface_event_skip;
                                const bool event_on_interval =
                                    event_after_skip &&
                                    ((matched_events -
                                      debug_nv2a_surface_event_skip - 1) %
                                         debug_nv2a_surface_event_interval ==
                                     0);
                                if (event_on_interval &&
                                    logged_events <
                                        debug_nv2a_surface_event_count) {
                                    ++logged_events;
                                    std::printf(
                                        "[debug-nv2a-surface-event] kind=%s "
                                        "target=%08x event=%llu insns=%llu "
                                        "seq=%llu profile=%u "
                                        "fp=%016llx full=%016llx prim=%u verts=%u "
                                        "sfmt=%08x shader=%08x comb=%08x "
                                        "en=%08x alpha=%x/%02x blend=%x/%x/%x "
                                        "depth=%x/%x texen=%x "
                                        "ci0=%08x ai0=%08x co0=%08x ao0=%08x "
                                        "f0_0=%08x f1_0=%08x final=%08x/%08x "
                                        "tex0=%08x/%08x/%08x/%08x/%08x/%08x "
                                        "frags=%llu writes=%llu\n",
                                        info.kind, info.color_address,
                                        static_cast<unsigned long long>(matched_events),
                                        static_cast<unsigned long long>(
                                            mach.total_insns()),
                                        static_cast<unsigned long long>(info.sequence),
                                        info.profile,
                                        static_cast<unsigned long long>(info.fingerprint),
                                        static_cast<unsigned long long>(
                                            info.full_state_hash),
                                        info.primitive_mode, info.vertex_count,
                                        info.surface_format,
                                        info.shader_stage_program,
                                        info.combiner_control,
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterEnables),
                                        info.alpha_func, info.alpha_ref,
                                        info.blend_src, info.blend_dst,
                                        info.blend_equation, info.depth_func,
                                        info.depth_mask, info.texture_enable_mask,
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterCombinerColorIcw0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterCombinerAlphaIcw0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterCombinerColorOcw0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterCombinerAlphaOcw0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterCombinerFactor0_0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterCombinerFactor1_0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterCombinerSpecularFog0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterCombinerSpecularFog1),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterTextureOffset0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterTextureFormat0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterTextureImageRect0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterTextureAddress0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterTextureControl0_0),
                                        mach.nv2a()->debug_raster_state_word(
                                            Nv2a::kRasterTextureControl1_0),
                                        static_cast<unsigned long long>(info.fragments),
                                        static_cast<unsigned long long>(info.writes));
                                    for (uint32_t vertex = 0;
                                         vertex < info.debug_vertex_count; ++vertex) {
                                        std::printf(
                                            "[debug-nv2a-surface-event-vertex] "
                                            "event=%llu v=%u "
                                            "pos=%g,%g,%g,%g "
                                            "diff=%g,%g,%g,%g "
                                            "tex0=%g,%g,%g,%g\n",
                                            static_cast<unsigned long long>(
                                                matched_events),
                                            vertex,
                                            info.debug_position[vertex][0],
                                            info.debug_position[vertex][1],
                                            info.debug_position[vertex][2],
                                            info.debug_position[vertex][3],
                                            info.debug_diffuse[vertex][0],
                                            info.debug_diffuse[vertex][1],
                                            info.debug_diffuse[vertex][2],
                                            info.debug_diffuse[vertex][3],
                                            info.debug_tex0[vertex][0],
                                            info.debug_tex0[vertex][1],
                                            info.debug_tex0[vertex][2],
                                            info.debug_tex0[vertex][3]);
                                    }
                                }
                            }
                            if (!snapshot_target) return;
                            if (info.writes <
                                debug_nv2a_surface_snapshot_min_writes) {
                                return;
                            }
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
                                    "alpha=%x/%02x blend=%x/%x/%x "
                                    "depth=%x/%x texen=%x "
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
                                    info.alpha_func, info.alpha_ref,
                                    info.blend_src, info.blend_dst,
                                    info.blend_equation, info.depth_func,
                                    info.depth_mask, info.texture_enable_mask,
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
                    "skip=%llu count=%llu interval=%llu min-writes=%llu\n",
                    debug_nv2a_surface_snapshot_prefix,
                    debug_nv2a_surface_snapshot_address,
                    static_cast<unsigned long long>(
                        debug_nv2a_surface_snapshot_skip),
                    static_cast<unsigned long long>(
                        debug_nv2a_surface_snapshot_count),
                    static_cast<unsigned long long>(
                        debug_nv2a_surface_snapshot_interval),
                    static_cast<unsigned long long>(
                        debug_nv2a_surface_snapshot_min_writes));
            }
            if (debug_nv2a_surface_events && mach.nv2a()) {
                std::printf(
                    "[host] NV2A surface events armed: target=%08x skip=%llu "
                    "count=%llu interval=%llu after-insns=%llu\n",
                    debug_nv2a_surface_event_address,
                    static_cast<unsigned long long>(
                        debug_nv2a_surface_event_skip),
                    static_cast<unsigned long long>(
                        debug_nv2a_surface_event_count),
                    static_cast<unsigned long long>(
                        debug_nv2a_surface_event_interval),
                    static_cast<unsigned long long>(
                        debug_nv2a_surface_event_after_insns));
            }
            if (hdd) {
                if (!mach.attach_hdd(hdd, hdd_writeback)) {
                    std::fprintf(stderr, "[hdd] attach failed: %s\n", hdd);
                    return 1;
                }
                // The write policy is part of the run's identity: the retail boot increments
                // the LBA-3 "BRFR" boot counter and writes it straight back, so a writeback
                // run changes its own input for the NEXT run (beads-xbox.1.14.11).
                std::printf("[hdd] attached %s (IDE drive 0, security LOCKED, writes=%s)\n",
                            hdd, hdd_writeback ? "PERSISTED to the image"
                                               : "copy-on-write, image never modified");
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
            // Stage (d) seam census: previous checkpoint's totals, so each line
            // is an interval delta (methodology section 2 rule 9 -- an instrument
            // carries its own denominators).
            SeamCensusSnapshot seam_prev;
            auto frame_series_interval_start = std::chrono::steady_clock::now();
            uint64_t frame_series_vblanks_start =
                mach.nv2a() ? mach.nv2a()->vblanks() : 0;
            // Idle census reporting. Always on (NOT gated on --perf-accounting or on the
            // retire-diag switch): the machine-side accumulators cost one compare plus one
            // 4-byte guest read per CPU slice. Printed as INTERVAL deltas at every
            // frame-series checkpoint so a per-window idle fraction is directly readable, and
            // once cumulatively at end of run.
            XboxMachine::IdleCensus idle_census_prev{};
            // Block-tier denominators travel with the census so every claim about the
            // instruction clock carries its own counts (PERF-METHODOLOGY rule 9). They are
            // always on: `tb_icount_misses` is the exactness self-check and must read 0.
            UnicornFloor* unicorn_floor = dynamic_cast<UnicornFloor*>(&mach.cpu());
            if (unicorn_floor && cpu_insn_hook) unicorn_floor->force_insn_scheduling();
            xlle_guest_tsc_deterministic = legacy_host_tsc ? 0 : 1;
            std::printf(
                "[cpu] scheduling=%s guest-tsc=%s\n",
                (unicorn_floor && unicorn_floor->insn_scheduling()) ? "per-instruction"
                                                                   : "per-block",
                legacy_host_tsc ? "host-rdtsc-passthrough (NON-DETERMINISTIC)"
                                : "retired-instruction clock");
            // Always-on translation-block PC ring (PRINCIPLES 7). Read backwards after the
            // fact; never armed. Prints the last `n` block entries, oldest first.
            auto print_block_pc_ring = [&](const char* tag, size_t n) {
                if (!unicorn_floor) return;
                const uint64_t writes = unicorn_floor->block_pc_ring_writes();
                const uint32_t* ring = unicorn_floor->block_pc_ring();
                if (!writes || !ring) return;
                const size_t cap = UnicornFloor::kBlockPcRing;
                const size_t have = writes < cap ? static_cast<size_t>(writes) : cap;
                if (n > have) n = have;
                std::printf("[ring] block-pc %s last=%zu of %llu:", tag, n,
                            static_cast<unsigned long long>(writes));
                for (size_t i = 0; i < n; ++i)
                    std::printf(" %08x", ring[(writes - n + i) & (cap - 1)]);
                std::printf("\n");
            };
            UnicornFloor::BlockStats block_stats_prev{};
            uint64_t idle_census_prev_irq0 = 0;
            uint64_t idle_census_prev_injected = 0;
            uint64_t idle_census_prev_pending = 0;
            auto print_idle_census = [&](const char* scope, unsigned long long deadline,
                                         bool interval) {
                const XboxMachine::IdleCensus& now = mach.idle_census();
                const XboxMachine::IdleCensus& was = idle_census_prev;
                const auto delta = [interval](uint64_t a, uint64_t b) {
                    return interval ? a - b : a;
                };
                const auto pct = [](uint64_t a, uint64_t b) {
                    return b ? 100.0 * static_cast<double>(a) / static_cast<double>(b) : 0.0;
                };
                const uint64_t slices = delta(now.slices, was.slices);
                const uint64_t insns = delta(now.insns, was.insns);
                const uint64_t idle_slices = delta(now.idle_slices, was.idle_slices);
                const uint64_t idle_insns = delta(now.idle_insns, was.idle_insns);
                const UnicornFloor::BlockStats bs =
                    unicorn_floor ? unicorn_floor->block_stats() : UnicornFloor::BlockStats{};
                const UnicornFloor::BlockStats& bsw = block_stats_prev;
                std::printf(
                    "[host] idle-census scope=%s deadline=%llu slices=%llu insns=%llu "
                    "idle_eip_slices=%llu idle_slice_pct=%.3f idle_insns=%llu "
                    "idle_insn_pct=%.3f early_stops=%llu early_stops_irq=%llu "
                    "budget_slack=%llu "
                    "thread_reads=%llu irq0=%llu injected=%llu pending_boundaries=%llu "
                    "tb_entries=%llu tb_insns=%llu tb_icount_entries=%llu "
                    "tb_icount_misses=%llu tb_icount_evictions=%llu tb_deadline_stops=%llu "
                    "tb_irq_window_stops=%llu tb_bootstrap_blocks=%llu "
                    "tb_skipped_insns=%llu "
                    "runs_insn_mode=%llu runs_block_mode=%llu "
                    "guest_tsc_reads=%llu guest_tsc_deterministic=%d "
                    "ff_on=%d ff_skips=%llu ff_insns=%llu ff_iters=%llu ff_insn_pct=%.3f "
                    "ff_max_insns=%llu ff_at_phase=%llu ff_p0=%llu ff_p1=%llu ff_p2=%llu ff_p6=%llu "
                    "ff_rej_flags=%llu ff_rej_state=%llu "
                    "ff_rej_short=%llu ff_rej_read=%llu ff_rej_bytes=%llu "
                    "idle_range=0x%08x-0x%08x "
                    "det_digest=%016llx det_slices=%llu "
                    "dev_digest=%016llx dev_events=%llu "
                    "dma_digest=%016llx dma_writes=%llu",
                    scope, deadline,
                    static_cast<unsigned long long>(slices),
                    static_cast<unsigned long long>(insns),
                    static_cast<unsigned long long>(idle_slices), pct(idle_slices, slices),
                    static_cast<unsigned long long>(idle_insns), pct(idle_insns, insns),
                    static_cast<unsigned long long>(delta(now.early_stops, was.early_stops)),
                    static_cast<unsigned long long>(
                        delta(now.early_stops_irq_armed, was.early_stops_irq_armed)),
                    static_cast<unsigned long long>(delta(now.budget_slack, was.budget_slack)),
                    static_cast<unsigned long long>(delta(now.thread_reads, was.thread_reads)),
                    static_cast<unsigned long long>(
                        delta(mach.irq0_pulses(), idle_census_prev_irq0)),
                    static_cast<unsigned long long>(
                        delta(mach.injected_interrupts(), idle_census_prev_injected)),
                    static_cast<unsigned long long>(
                        delta(mach.pending_boundaries(), idle_census_prev_pending)),
                    static_cast<unsigned long long>(delta(bs.tb_entries, bsw.tb_entries)),
                    static_cast<unsigned long long>(delta(bs.tb_insns, bsw.tb_insns)),
                    static_cast<unsigned long long>(bs.icount_entries),
                    static_cast<unsigned long long>(bs.icount_misses),
                    static_cast<unsigned long long>(bs.icount_evictions),
                    static_cast<unsigned long long>(
                        delta(bs.deadline_stops, bsw.deadline_stops)),
                    static_cast<unsigned long long>(
                        delta(bs.irq_window_stops, bsw.irq_window_stops)),
                    static_cast<unsigned long long>(bs.bootstrap_blocks),
                    static_cast<unsigned long long>(
                        delta(bs.skipped_insns, bsw.skipped_insns)),
                    static_cast<unsigned long long>(
                        delta(bs.insn_mode_runs, bsw.insn_mode_runs)),
                    static_cast<unsigned long long>(
                        delta(bs.block_mode_runs, bsw.block_mode_runs)),
                    static_cast<unsigned long long>(xlle_guest_tsc_reads),
                    xlle_guest_tsc_deterministic,
                    mach.idle_fast_forward() ? 1 : 0,
                    static_cast<unsigned long long>(delta(now.ff_skips, was.ff_skips)),
                    static_cast<unsigned long long>(delta(now.ff_insns, was.ff_insns)),
                    static_cast<unsigned long long>(delta(now.ff_iters, was.ff_iters)),
                    pct(delta(now.ff_insns, was.ff_insns), insns),
                    static_cast<unsigned long long>(now.ff_max_insns),
                    static_cast<unsigned long long>(delta(now.ff_at_head, was.ff_at_head)),
                    static_cast<unsigned long long>(delta(now.ff_phase[0], was.ff_phase[0])),
                    static_cast<unsigned long long>(delta(now.ff_phase[1], was.ff_phase[1])),
                    static_cast<unsigned long long>(delta(now.ff_phase[2], was.ff_phase[2])),
                    static_cast<unsigned long long>(delta(now.ff_phase[3], was.ff_phase[3])),
                    static_cast<unsigned long long>(delta(now.ff_rej_flags, was.ff_rej_flags)),
                    static_cast<unsigned long long>(delta(now.ff_rej_state, was.ff_rej_state)),
                    static_cast<unsigned long long>(delta(now.ff_rej_short, was.ff_rej_short)),
                    static_cast<unsigned long long>(delta(now.ff_rej_read, was.ff_rej_read)),
                    static_cast<unsigned long long>(delta(now.ff_rej_bytes, was.ff_rej_bytes)),
                    mach.idle_eip_lo(), mach.idle_eip_hi(),
                    static_cast<unsigned long long>(mach.det_digest()),
                    static_cast<unsigned long long>(mach.det_slices()),
                    static_cast<unsigned long long>(mach.dev_digest()),
                    static_cast<unsigned long long>(mach.dev_events()),
                    static_cast<unsigned long long>(mach.dma_digest()),
                    static_cast<unsigned long long>(mach.dma_writes()));
                struct CensusRow {
                    uint64_t value;
                    uint32_t key;
                };
                std::vector<CensusRow> rows;
                const auto emit_rows = [&](const char* label, size_t cap) {
                    std::sort(rows.begin(), rows.end(),
                              [](const CensusRow& a, const CensusRow& b) {
                                  return a.value > b.value;
                              });
                    std::printf(" %s:", label);
                    if (rows.empty()) std::printf("none");
                    for (size_t i = 0; i < rows.size() && i < cap; ++i)
                        std::printf("%s0x%08x=%llu", i ? "," : "", rows[i].key,
                                    static_cast<unsigned long long>(rows[i].value));
                };
                for (uint32_t i = 0; i < now.thread_count; ++i) {
                    const uint64_t v = delta(now.threads[i].insns, was.threads[i].insns);
                    if (v) rows.push_back({v, now.threads[i].thread});
                }
                emit_rows("threads-insns", 16);
                rows.clear();
                for (uint32_t i = 0; i < now.page_count; ++i) {
                    const uint64_t v = delta(now.pages[i].insns, was.pages[i].insns);
                    if (v) rows.push_back({v, now.pages[i].page});
                }
                emit_rows("boundary-pages-insns", 12);
                std::printf(" pages_dropped=%llu threads_dropped=%llu\n",
                            static_cast<unsigned long long>(
                                delta(now.pages_dropped, was.pages_dropped)),
                            static_cast<unsigned long long>(
                                delta(now.threads_dropped, was.threads_dropped)));
                idle_census_prev = now;
                block_stats_prev = bs;
                idle_census_prev_irq0 = mach.irq0_pulses();
                idle_census_prev_injected = mach.injected_interrupts();
                idle_census_prev_pending = mach.pending_boundaries();
            };
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
                    uint64_t block_regions = 0;
                    unicorn->hook_region_counts(code_regions, count_regions, block_regions);
                    const UnicornFloor::BlockStats bs = unicorn->block_stats();
                    // code-hook / count-hook regions are 0 on the canonical path by design
                    // (beads-xbox.1.4.6): no per-instruction hook is installed there. The
                    // block-tier denominators travel on the same line so any claim about the
                    // block clock carries its own counts (PERF-METHODOLOGY rule 9).
                    std::printf(
                        "[unicorn-memory] code-hook-regions=%llu "
                        "count-hook-regions=%llu block-hook-regions=%llu "
                        "tb-entries=%llu tb-insns=%llu tb-icount-entries=%llu "
                        "tb-icount-misses=%llu tb-icount-evictions=%llu "
                        "tb-deadline-stops=%llu tb-irq-window-stops=%llu "
                        "tb-skipped-insns=%llu tb-skips=%llu "
                        "runs-insn-mode=%llu runs-block-mode=%llu\n",
                        static_cast<unsigned long long>(code_regions),
                        static_cast<unsigned long long>(count_regions),
                        static_cast<unsigned long long>(block_regions),
                        static_cast<unsigned long long>(bs.tb_entries),
                        static_cast<unsigned long long>(bs.tb_insns),
                        static_cast<unsigned long long>(bs.icount_entries),
                        static_cast<unsigned long long>(bs.icount_misses),
                        static_cast<unsigned long long>(bs.icount_evictions),
                        static_cast<unsigned long long>(bs.deadline_stops),
                        static_cast<unsigned long long>(bs.irq_window_stops),
                        static_cast<unsigned long long>(bs.skipped_insns),
                        static_cast<unsigned long long>(bs.skips),
                        static_cast<unsigned long long>(bs.insn_mode_runs),
                        static_cast<unsigned long long>(bs.block_mode_runs));
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
                std::printf("[perf-generic] fingerprints=%zu top%zu=",
                            fingerprints.size(), perf_generic_top_limit);
                Nv2a::RasterStateSample raster_exemplar;
                const size_t limit =
                    std::min<size_t>(fingerprints.size(), perf_generic_top_limit);
                for (size_t i = 0; i < limit; ++i) {
                    const auto& entry = fingerprints[i];
                    std::printf(" %016llx:b%llu/f%llu/w%llu/cr%llu/dr%llu/ns%llu",
                                static_cast<unsigned long long>(entry.fingerprint),
                                static_cast<unsigned long long>(entry.batches),
                                static_cast<unsigned long long>(entry.fragments),
                                static_cast<unsigned long long>(entry.writes),
                                static_cast<unsigned long long>(entry.color_reads),
                                static_cast<unsigned long long>(entry.depth_reads),
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
                struct GenericFamilyKey {
                    uint32_t enables = 0;
                    uint32_t surface_format = 0;
                    uint32_t surface_pitch = 0;
                    uint32_t surface_color = 0;
                    uint32_t shader = 0;
                    uint32_t combiner = 0;
                    uint8_t primitive = 0;
                    std::array<uint32_t, 4> texture_format{};
                    bool operator==(const GenericFamilyKey& other) const {
                        return enables == other.enables &&
                               surface_format == other.surface_format &&
                               surface_pitch == other.surface_pitch &&
                               surface_color == other.surface_color &&
                               shader == other.shader &&
                               combiner == other.combiner &&
                               primitive == other.primitive &&
                               texture_format == other.texture_format;
                    }
                };
                struct GenericFamilyStats {
                    GenericFamilyKey key;
                    uint64_t fingerprints = 0;
                    uint64_t batches = 0;
                    uint64_t fragments = 0;
                    uint64_t writes = 0;
                    uint64_t color_reads = 0;
                    uint64_t depth_reads = 0;
                    uint64_t wall_ns = 0;
                    uint64_t texture_setup_ns = 0;
                    uint64_t software_raster_ns = 0;
                    uint64_t flush_ns = 0;
                    uint64_t bbox_pixels = 0;
                    uint32_t min_vertices = UINT32_MAX;
                    uint32_t max_vertices = 0;
                    uint64_t texture_modulate_batches = 0;
                    uint64_t texture_modulate_exact_batches = 0;
                    std::array<uint64_t,
                               static_cast<size_t>(
                                   Nv2a::GenericTextureModulateRejectReason::kCount)>
                        texture_modulate_reject_batches{};
                    uint64_t resident_batches = 0;
                    uint64_t resident_exact_batches = 0;
                    std::array<uint64_t,
                               static_cast<size_t>(
                                   Nv2a::GenericResidentRejectReason::kCount)>
                        resident_reject_batches{};
                };
                std::vector<GenericFamilyStats> families;
                for (const auto& entry : fingerprints) {
                    if (entry.raster_state_sample_index == UINT32_MAX ||
                        !mach.nv2a()->raster_state_sample_at(
                            entry.raster_state_sample_index, raster_exemplar))
                        continue;
                    const auto& state = raster_exemplar.words;
                    GenericFamilyKey key;
                    key.enables = state[Nv2a::kRasterEnables];
                    key.surface_format = state[Nv2a::kRasterSurfaceFormat];
                    key.surface_pitch = state[Nv2a::kRasterSurfacePitch];
                    key.surface_color = state[Nv2a::kRasterSurfaceColorOffset];
                    key.shader = state[Nv2a::kRasterShaderStageProgram];
                    key.combiner = state[Nv2a::kRasterCombinerControl];
                    key.primitive = entry.primitive_mode;
                    for (size_t stage = 0; stage < key.texture_format.size();
                         ++stage) {
                        key.texture_format[stage] =
                            state[Nv2a::kRasterTextureFormat0 + stage];
                    }
                    auto family = std::find_if(
                        families.begin(), families.end(),
                        [&](const auto& candidate) { return candidate.key == key; });
                    if (family == families.end()) {
                        GenericFamilyStats stats;
                        stats.key = key;
                        families.push_back(stats);
                        family = families.end() - 1;
                    }
                    ++family->fingerprints;
                    family->batches += entry.batches;
                    family->fragments += entry.fragments;
                    family->writes += entry.writes;
                    family->color_reads += entry.color_reads;
                    family->depth_reads += entry.depth_reads;
                    family->wall_ns += entry.wall_ns;
                    family->texture_setup_ns += entry.texture_setup_ns;
                    family->software_raster_ns += entry.software_raster_ns;
                    family->flush_ns += entry.flush_ns;
                    family->bbox_pixels += entry.bbox_pixels;
                    family->min_vertices =
                        std::min(family->min_vertices, entry.min_vertices);
                    family->max_vertices =
                        std::max(family->max_vertices, entry.max_vertices);
                    family->texture_modulate_batches +=
                        entry.texture_modulate_batches;
                    family->texture_modulate_exact_batches +=
                        entry.texture_modulate_exact_batches;
                    for (size_t reason = 0;
                         reason <
                         Nv2a::generic_texture_modulate_reject_reason_count();
                         ++reason) {
                        family->texture_modulate_reject_batches[reason] +=
                            entry.texture_modulate_reject_batches[reason];
                    }
                    family->resident_batches += entry.resident_batches;
                    family->resident_exact_batches += entry.resident_exact_batches;
                    for (size_t reason = 0;
                         reason < Nv2a::generic_resident_reject_reason_count();
                         ++reason) {
                        family->resident_reject_batches[reason] +=
                            entry.resident_reject_batches[reason];
                    }
                }
                std::sort(families.begin(), families.end(),
                          [](const auto& a, const auto& b) {
                              if (a.wall_ns != b.wall_ns) return a.wall_ns > b.wall_ns;
                              if (a.fragments != b.fragments)
                                  return a.fragments > b.fragments;
                              return a.batches > b.batches;
                          });
                std::printf("[perf-generic-family] families=%zu top%zu=",
                            families.size(), perf_generic_top_limit);
                const size_t family_limit =
                    std::min<size_t>(families.size(), perf_generic_top_limit);
                for (size_t i = 0; i < family_limit; ++i) {
                    const auto& family = families[i];
                    const auto& key = family.key;
                    std::printf(
                        " surf%08x/sfmt%08x/pitch%08x/prim%u/en%08x/sh%08x/comb%08x"
                        "/tf%08x,%08x,%08x,%08x:fp%llu/b%llu/f%llu/w%llu/cr%llu/dr%llu/ns%llu"
                        "/bb%llu/setup%llu/soft%llu/flush%llu/v%u-%u",
                        key.surface_color, key.surface_format, key.surface_pitch,
                        static_cast<unsigned>(key.primitive), key.enables,
                        key.shader, key.combiner, key.texture_format[0],
                        key.texture_format[1], key.texture_format[2],
                        key.texture_format[3],
                        static_cast<unsigned long long>(family.fingerprints),
                        static_cast<unsigned long long>(family.batches),
                        static_cast<unsigned long long>(family.fragments),
                        static_cast<unsigned long long>(family.writes),
                        static_cast<unsigned long long>(family.color_reads),
                        static_cast<unsigned long long>(family.depth_reads),
                        static_cast<unsigned long long>(family.wall_ns),
                        static_cast<unsigned long long>(family.bbox_pixels),
                        static_cast<unsigned long long>(family.texture_setup_ns),
                        static_cast<unsigned long long>(family.software_raster_ns),
                        static_cast<unsigned long long>(family.flush_ns),
                        family.min_vertices == UINT32_MAX ? 0 : family.min_vertices,
                        family.max_vertices);
                    if (family.texture_modulate_batches) {
                        std::printf("/texmod%llu",
                                    static_cast<unsigned long long>(
                                        family.texture_modulate_batches));
                        if (family.texture_modulate_exact_batches) {
                            std::printf("/exact%llu",
                                        static_cast<unsigned long long>(
                                            family.texture_modulate_exact_batches));
                        }
                        bool any_texmod_reject = false;
                        for (size_t reason = 0;
                             reason <
                             Nv2a::generic_texture_modulate_reject_reason_count();
                             ++reason) {
                            const uint64_t count =
                                family.texture_modulate_reject_batches[reason];
                            if (!count) continue;
                            std::printf("%s%s:%llu",
                                        any_texmod_reject ? "," : "/reject=",
                                        Nv2a::generic_texture_modulate_reject_reason_name(
                                            reason),
                                        static_cast<unsigned long long>(count));
                            any_texmod_reject = true;
                        }
                    }
                    if (family.resident_batches) {
                        std::printf("/resident%llu",
                                    static_cast<unsigned long long>(
                                        family.resident_batches));
                        if (family.resident_exact_batches) {
                            std::printf("/exact%llu",
                                        static_cast<unsigned long long>(
                                            family.resident_exact_batches));
                        }
                        bool any_resident_reject = false;
                        for (size_t reason = 0;
                             reason < Nv2a::generic_resident_reject_reason_count();
                             ++reason) {
                            const uint64_t count =
                                family.resident_reject_batches[reason];
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
                // Keep the compact default top-12 line readable, but emit complete,
                // machine-parseable exemplars for the requested hottest states so a
                // later backend can reproduce a dominant dashboard envelope
                // without another multi-billion-instruction replay.
                const size_t state_limit =
                    std::min<size_t>(fingerprints.size(), perf_generic_top_limit);
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

                // Checkpoint work -- scanout capture, PPM write, report formatting and
                // printing -- is diagnostics, not emulation, and capture_frame_series()
                // runs inside host_poll_cb_. Without this region every checkpoint's cost
                // would land in HOST_POLL and corrupt the partition (beads-xbox.1.14.3).
                xlle::perf_excl::Scope diag_region(xlle::perf_excl::kDiagCheckpoint);
                // Seam anchor 10: the frame-series checkpoint is a guest-anchored
                // gate point -- it reads the framebuffer and every counter.
                mach.nv2a()->seam_checkpoint_anchor();
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
                print_seam_census(mach.nv2a(), "interval",
                                  static_cast<unsigned long long>(deadline),
                                  seam_prev);
                print_gpu_residency_census(
                    mach.nv2a(), "cumulative",
                    static_cast<unsigned long long>(deadline),
                    mach.nv2a() ? mach.nv2a()->seam_vblanks() : 0);
                print_idle_census("interval", static_cast<unsigned long long>(deadline),
                                  /*interval=*/true);
                // Exclusive-time partition, as INTERVAL deltas (beads-xbox.1.14.3). The
                // [perf...] lines below are the legacy inclusive buckets and are kept
                // verbatim for tool compatibility; this line is the one that partitions.
                const std::string perf_excl_report = xlle::perf_excl::format_report(
                    "interval", static_cast<unsigned long long>(deadline),
                    /*interval=*/true);
                if (!perf_excl_report.empty())
                    std::printf("%s\n", perf_excl_report.c_str());
                const std::string perf_report = xlle::perf::format_report();
                if (!perf_report.empty()) std::printf("%s\n", perf_report.c_str());
                print_generic_combiner_perf_report();
                const std::string witnessed_profile_report =
                    xlle::perf::format_witnessed_profile_report();
                if (!witnessed_profile_report.empty())
                    std::printf("%s\n", witnessed_profile_report.c_str());
                print_resident_combiner_report();
                const std::string memory_report =
                    xlle::perf::format_memory_report();
                if (!memory_report.empty())
                    std::printf("%s\n", memory_report.c_str());
                const std::string generic_split_report =
                    xlle::perf::format_generic_split_report();
                if (!generic_split_report.empty())
                    std::printf("%s\n", generic_split_report.c_str());
                const std::string generic_detail_report =
                    xlle::perf::format_generic_detail_report();
                if (!generic_detail_report.empty())
                    std::printf("%s\n", generic_detail_report.c_str());
                const std::string generic_admission_report =
                    xlle::perf::format_generic_admission_report();
                if (!generic_admission_report.empty())
                    std::printf("%s\n", generic_admission_report.c_str());
                const std::string generic_texture_detail_report =
                    xlle::perf::format_generic_texture_detail_report();
                if (!generic_texture_detail_report.empty())
                    std::printf("%s\n", generic_texture_detail_report.c_str());
                const std::string nv2a_split_report =
                    xlle::perf::format_nv2a_split_report();
                if (!nv2a_split_report.empty())
                    std::printf("%s\n", nv2a_split_report.c_str());
                const std::string renderer_report =
                    xlle::perf::format_renderer_report();
                if (!renderer_report.empty())
                    std::printf("%s\n", renderer_report.c_str());
                const std::string renderer_resident_detail_report =
                    xlle::perf::format_renderer_resident_detail_report();
                if (!renderer_resident_detail_report.empty())
                    std::printf("%s\n", renderer_resident_detail_report.c_str());
                const std::string renderer_resolve_context_report =
                    xlle::perf::format_renderer_resolve_context_report();
                if (!renderer_resolve_context_report.empty())
                    std::printf("%s\n",
                                renderer_resolve_context_report.c_str());
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
                    std::printf(
                        "[dbg] guest vector 0x%02x raised at eip=0x%08x eax=0x%08x "
                        "eflags=0x%08x insns=%llu\n",
                        vec, ir.eip, ir.eax, ir.eflags,
                        static_cast<unsigned long long>(mach.total_insns()));
                    print_block_pc_ring("at-vector", 48);
                    if (++vec_logged == 32) std::printf("[dbg] (vector log capped)\n");
                }
            });
            X86Regs r{};
            r.eip = 0x901ee;
            r.esp = 0x00400000;  // scratch stack in RAM (2BL sets its own later)
            r.eflags = 0x2;
            mach.cpu().set_regs(r);
            mach.cpu().setup_flat_segments(0x1000);  // MCPX handoff GDT (code=0x08, data=0x10)
            // Calibrate the exclusive-time clock BEFORE arming it, so the first measured
            // region is not the one paying for calibration, and so wall_ns starts here
            // rather than at the first region.
            // beads-xbox.1.15.10 phase 5: the calibration is now UNCONDITIONAL,
            // and the comment below is kept because it records why it used to
            // be gated. init() spin-waits ~4 ms against steady_clock and takes
            // 8192 rdtsc reads -- a fixed ~4 ms charged once per process, which
            // is 0.007 % of a 59 s replay leg and does not move the gate metric.
            // It has to be unconditional now because the ALWAYS-ON residency and
            // vertex censuses (Nv2a::ResolveQueryCensus / VertexCensus) report in
            // nanoseconds, and without calibration g_ns_per_tick is 1.0 and every
            // always-on line in a noperf run would be a raw TSC tick count
            // silently labelled `ns`. g_enabled stays gated on --perf-accounting
            // below, so no perf_excl::Scope becomes active: this call changes
            // the units the always-on lines are printed in and nothing else.
            xlle::perf_excl::init();
            xlle::perf_excl::g_enabled = perf_accounting;
            xlle::perf::g_enabled = perf_accounting;
            xlle::perf::g_pull_drain_enabled =
                perf_accounting && perf_pull_drain;
            std::printf("[2bl] entering decrypted 2BL at 0x901ee (budget %llu insns) ...\n",
                        (unsigned long long)max_insns);
            std::string err2;
            HostSampleProfiler host_sampler;
            if (host_sample_profile_ms) {
                host_sampler.set_progress_source(&mach.progress_insns());
                host_sampler.start(host_sample_profile_ms);
            }
            uint64_t n = mach.run(0x901ee, max_insns, err2);
            if (host_sample_profile_ms) host_sampler.stop_and_report();
            const bool host_memory_safety_stop =
                err2.find("host memory reached soft limit") != std::string::npos ||
                err2.find("GetProcessMemoryInfo failed while enforcing host memory limit") !=
                    std::string::npos;
            if (host_memory_safety_stop) {
                // Preserve the soft-to-hard-limit headroom. Full scanout, raster-state,
                // scheduler, and stack diagnostics allocate temporary vectors and can turn
                // a controlled memory stop into std::bad_alloc before teardown.
                // Exclusive-time partition, run totals (beads-xbox.1.14.3).
                const std::string perf_excl_report = xlle::perf_excl::format_report(
                    "cumulative",
                    static_cast<unsigned long long>(mach.total_insns()),
                    /*interval=*/false);
                if (!perf_excl_report.empty())
                    std::printf("%s\n", perf_excl_report.c_str());
                const std::string perf_report = xlle::perf::format_report();
                if (!perf_report.empty()) std::printf("%s\n", perf_report.c_str());
                print_generic_combiner_perf_report();
                const std::string witnessed_profile_report =
                    xlle::perf::format_witnessed_profile_report();
                if (!witnessed_profile_report.empty())
                    std::printf("%s\n", witnessed_profile_report.c_str());
                const std::string memory_report = xlle::perf::format_memory_report();
                if (!memory_report.empty()) std::printf("%s\n", memory_report.c_str());
                const std::string generic_split_report =
                    xlle::perf::format_generic_split_report();
                if (!generic_split_report.empty())
                    std::printf("%s\n", generic_split_report.c_str());
                const std::string generic_detail_report =
                    xlle::perf::format_generic_detail_report();
                if (!generic_detail_report.empty())
                    std::printf("%s\n", generic_detail_report.c_str());
                const std::string generic_admission_report =
                    xlle::perf::format_generic_admission_report();
                if (!generic_admission_report.empty())
                    std::printf("%s\n", generic_admission_report.c_str());
                const std::string generic_texture_detail_report =
                    xlle::perf::format_generic_texture_detail_report();
                if (!generic_texture_detail_report.empty())
                    std::printf("%s\n", generic_texture_detail_report.c_str());
                const std::string nv2a_split_report =
                    xlle::perf::format_nv2a_split_report();
                if (!nv2a_split_report.empty())
                    std::printf("%s\n", nv2a_split_report.c_str());
                const std::string renderer_report =
                    xlle::perf::format_renderer_report();
                if (!renderer_report.empty())
                    std::printf("%s\n", renderer_report.c_str());
                const std::string renderer_resident_detail_report =
                    xlle::perf::format_renderer_resident_detail_report();
                if (!renderer_resident_detail_report.empty())
                    std::printf("%s\n", renderer_resident_detail_report.c_str());
                const std::string renderer_resolve_context_report =
                    xlle::perf::format_renderer_resolve_context_report();
                if (!renderer_resolve_context_report.empty())
                    std::printf("%s\n",
                                renderer_resolve_context_report.c_str());
                const std::string gl_context_report =
                    xlle::perf::format_gl_context_report();
                if (!gl_context_report.empty())
                    std::printf("%s\n", gl_context_report.c_str());
                print_memory_diagnostics();
                host_input.close();
                const X86Regs stopped = mach.cpu().get_regs();
                std::printf(
                    "[2bl] retired %llu instructions; final eip=0x%08x eax=0x%08x "
                    "ecx=0x%08x\n",
                    static_cast<unsigned long long>(n), stopped.eip, stopped.eax,
                    stopped.ecx);
                std::printf("[2bl] STOPPED: %s\n", err2.c_str());
                print_block_pc_ring("at-stop", 96);
                std::printf(
                    "[host] allocation-heavy final diagnostics skipped after memory "
                    "safety stop\n");
                std::fflush(stdout);
                return 2;
            }
            // The run may cross its final series deadline after the last 1 kHz host poll.
            capture_frame_series();
            {
                // Exclusive-time partition, run totals (beads-xbox.1.14.3).
                const std::string perf_excl_report = xlle::perf_excl::format_report(
                    "cumulative",
                    static_cast<unsigned long long>(mach.total_insns()),
                    /*interval=*/false);
                if (!perf_excl_report.empty())
                    std::printf("%s\n", perf_excl_report.c_str());
                const std::string perf_report = xlle::perf::format_report();
                if (!perf_report.empty()) std::printf("%s\n", perf_report.c_str());
                print_generic_combiner_perf_report();
                const std::string witnessed_profile_report =
                    xlle::perf::format_witnessed_profile_report();
                if (!witnessed_profile_report.empty())
                    std::printf("%s\n", witnessed_profile_report.c_str());
                const std::string memory_report =
                    xlle::perf::format_memory_report();
                if (!memory_report.empty())
                    std::printf("%s\n", memory_report.c_str());
                const std::string generic_split_report =
                    xlle::perf::format_generic_split_report();
                if (!generic_split_report.empty())
                    std::printf("%s\n", generic_split_report.c_str());
                const std::string generic_detail_report =
                    xlle::perf::format_generic_detail_report();
                if (!generic_detail_report.empty())
                    std::printf("%s\n", generic_detail_report.c_str());
                const std::string generic_admission_report =
                    xlle::perf::format_generic_admission_report();
                if (!generic_admission_report.empty())
                    std::printf("%s\n", generic_admission_report.c_str());
                const std::string generic_texture_detail_report =
                    xlle::perf::format_generic_texture_detail_report();
                if (!generic_texture_detail_report.empty())
                    std::printf("%s\n", generic_texture_detail_report.c_str());
                const std::string nv2a_split_report =
                    xlle::perf::format_nv2a_split_report();
                if (!nv2a_split_report.empty())
                    std::printf("%s\n", nv2a_split_report.c_str());
                const std::string renderer_report =
                    xlle::perf::format_renderer_report();
                if (!renderer_report.empty())
                    std::printf("%s\n", renderer_report.c_str());
                const std::string renderer_resident_detail_report =
                    xlle::perf::format_renderer_resident_detail_report();
                if (!renderer_resident_detail_report.empty())
                    std::printf("%s\n", renderer_resident_detail_report.c_str());
                const std::string renderer_resolve_context_report =
                    xlle::perf::format_renderer_resolve_context_report();
                if (!renderer_resolve_context_report.empty())
                    std::printf("%s\n",
                                renderer_resolve_context_report.c_str());
                const std::string gl_context_report =
                    xlle::perf::format_gl_context_report();
                if (!gl_context_report.empty())
                    std::printf("%s\n", gl_context_report.c_str());
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
            // Seam anchor 10 (stage (e), beads-xbox.1.15.5): shutdown is a
            // guest-anchored gate point. Everything printed below -- the 24
            // profile hashes, every per-profile fragment/write tally, the
            // scanout itself -- is only complete once the draw worker has
            // finished and its jobs have been folded on this thread, so drain
            // before the first of them is read.
            if (mach.nv2a()) mach.nv2a()->seam_checkpoint_anchor();
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
            if (!err2.empty()) {
                std::printf("[2bl] STOPPED: %s\n", err2.c_str());
                print_block_pc_ring("at-stop", 96);
            }
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
                auto print_2d_methods = [&](uint8_t object_class, const char* name) {
                    bool any = false;
                    for (uint32_t method = 0; method < 0x2000; method += 4) {
                        auto stat = mach.nv2a()->class_method_stat(object_class, method);
                        if (!stat.count) continue;
                        if (!any) {
                            std::printf("[2bl] NV2A %s methods:\n", name);
                            any = true;
                        }
                        std::printf("[2bl]   class=0x%02x method=0x%04x count=%llu "
                                    "first=0x%08x last=0x%08x\n",
                                    object_class, method,
                                    (unsigned long long)stat.count,
                                    stat.first_parameter, stat.last_parameter);
                    }
                };
                print_2d_methods(0x62, "class62");
                print_2d_methods(0x9F, "class9f");
                print_2d_methods(0x44, "class44");
                std::printf("[2bl] NV2A image-blit: copies=%llu bytes=%llu "
                            "last-src=%08x last-dst=%08x last-size=%08x\n",
                            (unsigned long long)mach.nv2a()->image_blit_copies(),
                            (unsigned long long)mach.nv2a()->image_blit_bytes(),
                            mach.nv2a()->last_image_blit_source(),
                            mach.nv2a()->last_image_blit_dest(),
                            mach.nv2a()->last_image_blit_size());
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
                            "attempted/accelerated/fallback/software/exec/tri/frags/depth-pass/writes/color-reads/depth-reads/tex-samples="
                            "%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu "
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
                            (unsigned long long)mach.nv2a()->generic_combiner_color_reads(),
                            (unsigned long long)mach.nv2a()->generic_combiner_depth_reads(),
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
                {
                    // Stage (d) seam census, cumulative over the whole run.
                    SeamCensusSnapshot zero;
                    print_seam_census(mach.nv2a(), "cumulative",
                                      (unsigned long long)mach.total_insns(),
                                      zero);
                    print_gpu_residency_census(
                        mach.nv2a(), "cumulative",
                        (unsigned long long)mach.total_insns(),
                        mach.nv2a() ? mach.nv2a()->seam_vblanks() : 0);
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
                        "cow_sectors=%llu writeback=%d "
                        "bm_base=0x%04x\n", (unsigned long long)ide_io_count,
                        (unsigned long long)(mach.ide() ? mach.ide()->irq_pulses() : 0),
                        (unsigned long long)(mach.ide() ? mach.ide()->sectors_read() : 0),
                        (unsigned long long)(mach.ide() ? mach.ide()->sectors_written() : 0),
                        (unsigned long long)(mach.ide() ? mach.ide()->overlay_sectors() : 0),
                        mach.ide() && mach.ide()->hdd_writeback() ? 1 : 0,
                        mach.ide() ? mach.ide()->bm_base() : 0);
            std::printf("[2bl] MCPX ROM overlay disabled by firmware: %s\n",
                        mach.pci() && mach.pci()->mcpx_rom_disabled() ? "yes" : "no");
            std::printf("[2bl] IRQ0 pulses=%llu, pending-boundaries=%llu, delivered=%llu\n",
                        (unsigned long long)mach.irq0_pulses(),
                        (unsigned long long)mach.pending_boundaries(),
                        (unsigned long long)mach.injected_interrupts());
            print_idle_census("cumulative",
                              static_cast<unsigned long long>(mach.total_insns()),
                              /*interval=*/false);
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
            if (debug_memory_dump) {
                std::string memory_error;
                const bool memory_written = dump_guest_memory(
                    mach, debug_memory_dump, debug_memory_address,
                    debug_memory_size, memory_error);
                frame_output_ok = frame_output_ok && memory_written;
                std::printf("[host] debug memory dump %s addr=%08x size=%llu: "
                            "%s%s%s\n",
                            debug_memory_dump, debug_memory_address,
                            static_cast<unsigned long long>(debug_memory_size),
                            memory_written ? "written" : "FAILED",
                            memory_error.empty() ? "" : " (",
                            memory_error.empty() ? "" : (memory_error + ")").c_str());
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
