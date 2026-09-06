#include "host/nv2a_opengl_renderer.hpp"
#include "machine/nv2a_renderer.hpp"
#include "nv2a_vsh_batch_probe_cases.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if !XLLE_VSH_GPU_RESIDENT
int main() {
    std::printf("SKIP: XLLE_VSH_GPU_RESIDENT is not enabled in this build\n");
    return 2;
}
#else

namespace {

using xlle::Nv2aGenericCombinerPacket;
using xlle::Nv2aGenericCombinerResult;
using xlle::Nv2aGenericCombinerVertex;
using xlle::Nv2aRenderer;
namespace cases = xlle::vsh_shadow_probe;
namespace cap = xlle::vsh_batch_capture;
namespace gpu = xlle::vsh_gpu_batch;

constexpr uint32_t kWidth = 64;
constexpr uint32_t kHeight = 64;
constexpr uint32_t kPitch = kWidth * 4;
constexpr uint32_t kSurfacePitchWord = (kPitch << 16) | kPitch;
constexpr uint32_t kSurfaceFormatA8R8G8B8 = 0x128;
constexpr uint32_t kBaseAddress = 0x02000000;
constexpr uint8_t kInitialByte = 0xA8;

struct ProbeCase {
    std::string name;
    cap::Batch batch;
    bool x8 = false;
    bool expect_decline = false;
    bool require_changed = false;
    bool malformed_gpu_layout = false;
};

uint32_t bits(float value) {
    uint32_t out = 0;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

float from_bits(uint32_t word) {
    float out = 0.0f;
    std::memcpy(&out, &word, sizeof(out));
    return out;
}

std::array<float, 4> read_vec4(const uint32_t* words) {
    std::array<float, 4> out{};
    for (size_t i = 0; i < out.size(); ++i) out[i] = from_bits(words[i]);
    return out;
}

Nv2aGenericCombinerVertex vertex_from_execution(const cap::Execution& execution) {
    Nv2aGenericCombinerVertex vertex;
    vertex.position = read_vec4(execution.expected.data() + 28);
    vertex.diffuse = read_vec4(execution.expected.data() + 4);
    vertex.specular = read_vec4(execution.expected.data() + 8);
    for (size_t stage = 0; stage < 4; ++stage)
        vertex.texture[stage] = read_vec4(execution.expected.data() + 12 + stage * 4);
    return vertex;
}

std::vector<Nv2aGenericCombinerVertex> cpu_vertices_from_expected(
    const cap::Batch& batch) {
    std::vector<Nv2aGenericCombinerVertex> vertices;
    vertices.reserve(batch.expected_indices.size());
    for (uint32_t id : batch.expected_indices) {
        if (id >= batch.executions.size())
            throw std::runtime_error("synthetic expected index outside execution list");
        vertices.push_back(vertex_from_execution(batch.executions[id]));
    }
    return vertices;
}

std::shared_ptr<Nv2aGenericCombinerPacket::GpuVshReference> make_gpu_reference(
    const cap::Batch& source, uint32_t mode) {
    auto reference = std::make_shared<Nv2aGenericCombinerPacket::GpuVshReference>();
    reference->mode = mode;
    reference->batch.shape.primitive_mode = source.primitive_mode;
    reference->batch.shape.large_target = source.large_target;
    reference->batch.shape.cull_enabled = source.cull_enabled;
    reference->batch.shape.cull_front = source.cull_front;
    reference->batch.shape.op_count = static_cast<uint32_t>(source.ops.size());
    reference->batch.shape.execution_count = static_cast<uint32_t>(source.executions.size());
    reference->batch.shape.reference_count = static_cast<uint32_t>(source.references.size());
    reference->batch.ops = source.ops;
    reference->batch.references = source.references;
    reference->batch.executions.reserve(source.executions.size());
    reference->expected.reserve(source.executions.size());
    std::copy(source.constants.begin(), source.constants.end(),
              reference->batch.constants.words.begin());
    for (const auto& execution : source.executions) {
        gpu::ExecutionInput input;
        input.words = execution.inputs;
        reference->batch.executions.push_back(input);
        gpu::ProgramOutput128B expected;
        for (size_t i = 0; i < xlle::vsh_capture::kConsumedOutputs.size(); ++i) {
            for (size_t lane = 0; lane < 4; ++lane)
                expected.values[i][lane] = execution.expected[i * 4 + lane];
        }
        for (size_t lane = 0; lane < 4; ++lane)
            expected.values[7][lane] = execution.expected[28 + lane];
        reference->expected.push_back(expected);
    }
    return reference;
}

Nv2aGenericCombinerPacket make_packet(const cap::Batch& batch, uint32_t address,
                                      bool x8, uint32_t gpu_mode) {
    Nv2aGenericCombinerPacket packet;
    packet.compute_fixture = Nv2aGenericCombinerPacket::ComputeFixture::
        kResidentPrimaryColorTriangles;
    packet.primitive_mode = 5;
    packet.vertices = cpu_vertices_from_expected(batch);
    packet.surface_format = kSurfaceFormatA8R8G8B8;
    packet.surface_pitch = kSurfacePitchWord;
    packet.surface_height = kHeight;
    packet.color_address = address;
    packet.depth_address = address + 0x00100000u;
    packet.color_input.assign(static_cast<size_t>(kPitch) * kHeight, kInitialByte);
    packet.fixture_clip_x = 0;
    packet.fixture_clip_y = 0;
    packet.fixture_clip_width = kWidth;
    packet.fixture_clip_height = kHeight;
    packet.fixture_blend_enabled = false;
    packet.fixture_depth_test_enabled = false;
    packet.fixture_depth_write_enabled = false;
    packet.fixture_color_format_x8 = x8;
    if (gpu_mode) packet.gpu_vsh = make_gpu_reference(batch, gpu_mode);
    return packet;
}

bool submit_and_resolve(Nv2aRenderer& renderer, const Nv2aGenericCombinerPacket& packet,
                        std::vector<uint8_t>& out, std::string& error) {
    if (!renderer.supports_resident_generic_combiner(packet)) {
        error = "renderer declined resident primary packet";
        return false;
    }
    Nv2aGenericCombinerResult result;
    if (!renderer.submit_resident_generic_combiner(packet, result, error)) return false;
    const size_t expected_vertices = packet.gpu_vsh && packet.gpu_vsh->mode == 3
        ? packet.gpu_vsh->prepared_vertices : packet.vertices.size();
    if (result.triangles != expected_vertices / 3) {
        error = "renderer triangle result differs from submitted geometry";
        return false;
    }
    xlle::Nv2aResidentColorResolve resolve;
    if (!renderer.resolve_resident_color_surface(packet.color_address,
                                                 packet.color_input.size(), resolve,
                                                 error)) {
        return false;
    }
    out = std::move(resolve.data);
    return true;
}

size_t pixel_offset(uint32_t x, uint32_t y) {
    return (static_cast<size_t>(y) * kWidth + x) * 4u;
}

size_t changed_bytes(const std::vector<uint8_t>& data) {
    return static_cast<size_t>(std::count_if(data.begin(), data.end(), [](uint8_t b) {
        return b != kInitialByte;
    }));
}

bool assert_x8_alpha_repair(const std::string& name, const std::vector<uint8_t>& data,
                            std::string& detail) {
    const size_t interior = pixel_offset(16, 16);
    const size_t exterior = pixel_offset(60, 60);
    if (data.size() < exterior + 4u) {
        detail = name + ": resolved surface is smaller than expected";
        return false;
    }
    if (data[interior + 3u] != 0xffu) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer),
                      "%s: X8 drawn interior alpha was %u, expected 255",
                      name.c_str(), static_cast<unsigned>(data[interior + 3u]));
        detail = buffer;
        return false;
    }
    if (data[exterior + 3u] != kInitialByte) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer),
                      "%s: X8 untouched exterior alpha was %u, expected %u",
                      name.c_str(), static_cast<unsigned>(data[exterior + 3u]),
                      static_cast<unsigned>(kInitialByte));
        detail = buffer;
        return false;
    }
    return true;
}

bool equal_bytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b,
                 std::string& detail) {
    if (a.size() != b.size()) {
        detail = "size differs";
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer),
                          "first byte mismatch at %zu: cpu=%u gpu=%u",
                          i, static_cast<unsigned>(a[i]), static_cast<unsigned>(b[i]));
            detail = buffer;
            return false;
        }
    }
    return true;
}

cap::Execution exec(float x, float y, float z, float w,
                    std::array<float, 4> diffuse = {1.0f, 0.25f, 0.125f, 1.0f}) {
    return cases::detail::make_execution(x, y, z, w, diffuse);
}

cap::Batch uncached(uint32_t primitive, std::vector<cap::Execution> executions,
                    std::vector<uint32_t> expected) {
    return cases::detail::make_uncached(1, primitive, std::move(executions),
                                        std::move(expected));
}

ProbeCase visible_list(bool x8 = false) {
    const std::array<float, 4> diffuse = x8 ? std::array<float, 4>{1.0f, 0.25f, 0.125f, 0.25f}
                                           : std::array<float, 4>{1.0f, 0.25f, 0.125f, 1.0f};
    return {x8 ? "visible_list_x8_alpha_repair" : "visible_list",
            uncached(5, {exec(8, 8, 0, 1, diffuse), exec(40, 8, 0, 1, diffuse),
                         exec(8, 40, 0, 1, diffuse)}, {0, 1, 2}),
            x8, false, true};
}

ProbeCase large_target_visible() {
    auto batch = uncached(5, {exec(8, 8, 0, 1), exec(40, 8, 0, 1),
                              exec(8, 40, 0, 1)}, {0, 1, 2});
    batch.large_target = true;
    return {"large_target_visible_accept", std::move(batch), false, false, true};
}

ProbeCase large_target_out_of_bound_fallback() {
    auto batch = uncached(5, {exec(-62500.0625f, -62500.0625f, 0, 1),
                              exec(62500.0625f, -62500.0625f, 0, 1),
                              exec(32.0f, 62500.0625f, 0, 1)}, {0, 1, 2});
    batch.large_target = true;
    return {"large_target_oob_declines_to_cpu", std::move(batch), false, true, true};
}

ProbeCase malformed_gpu_wire_layout() {
    auto batch = uncached(5, {exec(8, 8, 0, 1), exec(40, 8, 0, 1),
                              exec(8, 40, 0, 1)}, {0, 1, 2});
    return {"malformed_gpu_wire_layout_declines_to_cpu", std::move(batch),
            false, true, true, true};
}

ProbeCase pack_boundary(uint32_t triangles, bool mixed_cull, bool late_decline = false) {
    std::vector<cap::Execution> executions;
    std::vector<uint32_t> expected;
    for (uint32_t i = 0; i < triangles; ++i) {
        const uint32_t base = static_cast<uint32_t>(executions.size());
        const std::array<float, 4> color{
            static_cast<float>(i % 251 + 1) / 256.0f,
            static_cast<float>(i / 251 + 1) / 256.0f, 0.125f, 1.0f};
        const bool culled = mixed_cull && i % 3 == 1;
        if (late_decline && i + 1 == triangles) {
            executions.push_back(exec(-62500.0625f, -62500.0625f, 0, 1, color));
            executions.push_back(exec(62500.0625f, -62500.0625f, 0, 1, color));
            executions.push_back(exec(32, 62500.0625f, 0, 1, color));
        } else {
            executions.push_back(exec(8, 8, 0, 1, color));
            executions.push_back(exec(culled ? 8 : 40, culled ? 40 : 8, 0, 1, color));
            executions.push_back(exec(culled ? 40 : 8, culled ? 8 : 40, 0, 1, color));
        }
        if (!culled) expected.insert(expected.end(), {base, base + 1, base + 2});
    }
    auto batch = uncached(5, std::move(executions), std::move(expected));
    batch.cull_enabled = mixed_cull;
    batch.large_target = late_decline;
    return {"pack_boundary_" + std::to_string(triangles) + (mixed_cull ? "_mixed" : "_all") +
            (late_decline ? "_late_decline" : ""), std::move(batch), false, late_decline, true};
}

std::vector<ProbeCase> make_probe_cases() {
    std::vector<ProbeCase> out;
    out.push_back(visible_list());
    out.push_back(visible_list(true));
    out.push_back(large_target_visible());
    out.push_back(large_target_out_of_bound_fallback());
    out.push_back(malformed_gpu_wire_layout());
    out.push_back(pack_boundary(255, false));
    out.push_back(pack_boundary(256, false));
    out.push_back(pack_boundary(257, false));
    out.push_back(pack_boundary(769, true));
    out.push_back(pack_boundary(5461, false));
    out.push_back(pack_boundary(257, false, true));
    out.push_back({"visible_strip", uncached(6,
        {exec(8, 8, 0, 1), exec(40, 8, 0, 1), exec(8, 40, 0, 1),
         exec(40, 40, 0, 1)}, {0, 1, 2, 2, 1, 3}), false, false, true});
    out.push_back({"visible_fan", uncached(7,
        {exec(24, 24, 0, 1), exec(40, 8, 0, 1), exec(40, 40, 0, 1),
         exec(8, 40, 0, 1)}, {0, 1, 2, 0, 2, 3}), false, false, true});

    auto cull = uncached(5,
        {exec(8, 8, 0, 1), exec(40, 8, 0, 1), exec(8, 40, 0, 1),
         exec(48, 8, 0, 1), exec(16, 8, 0, 1), exec(48, 40, 0, 1)},
        {0, 1, 2});
    cull.cull_enabled = true;
    out.push_back({"cull_compacts_second_triangle", std::move(cull), false, false, true});

    cap::Batch repeated;
    repeated.sequence = 1;
    repeated.primitive_mode = 5;
    repeated.cache_enabled = true;
    repeated.ops = cases::detail::mov_program();
    repeated.executions = {exec(8, 8, 0, 1), exec(40, 8, 0, 1),
                           exec(8, 40, 0, 1), exec(8, 8, 0, 1),
                           exec(40, 8, 0, 1), exec(40, 40, 0, 1)};
    repeated.source_indices = {70u, 1024u, 5u, 70u, 1u, 2u, 1u, 2u, 70u};
    repeated.references = {0u, 1u, 2u, 3u, 4u, 5u, 4u, 5u, 3u};
    repeated.expected_indices = {0u, 1u, 2u, 3u, 4u, 5u, 4u, 5u, 3u};
    out.push_back({"cache_collision_and_hit_refs", std::move(repeated), false, false, true});

    auto trailing1 = uncached(5,
        {exec(8, 8, 0, 1), exec(40, 8, 0, 1), exec(8, 40, 0, 1),
         exec(60, 60, 0, 1)}, {0, 1, 2});
    out.push_back({"triangle_list_trailing_one_ref", std::move(trailing1), false, false, true});
    auto trailing2 = uncached(5,
        {exec(8, 8, 0, 1), exec(40, 8, 0, 1), exec(8, 40, 0, 1),
         exec(60, 60, 0, 1), exec(61, 60, 0, 1)}, {0, 1, 2});
    out.push_back({"triangle_list_trailing_two_refs", std::move(trailing2), false, false, true});

    out.push_back({"positive_zero_w_draw", uncached(5,
        {exec(8, 8, 0, 0.0f), exec(40, 8, 0, 0.0f), exec(8, 40, 0, 0.0f)},
        {0, 1, 2}), false, false, true});

    auto nan_texture = uncached(5,
        {exec(8, 8, 0, 1), exec(40, 8, 0, 1), exec(8, 40, 0, 1)},
        {0, 1, 2});
    nan_texture.executions[0].inputs[9u * 4u] =
        bits(std::numeric_limits<float>::quiet_NaN());
    xlle::vsh_capture::Vertex v;
    v.ordinal = 1;
    v.ops = cases::detail::mov_program();
    v.inputs = nan_texture.executions[0].inputs;
    const auto raw = xlle::vsh_capture_oracle::validate_and_execute(v);
    std::copy(raw.begin(), raw.end(), nan_texture.executions[0].expected.begin());
    nan_texture.executions[0].expected[28] = bits(cases::detail::round_screen_coordinate(from_bits(raw[0])));
    nan_texture.executions[0].expected[29] = bits(cases::detail::round_screen_coordinate(from_bits(raw[1])));
    nan_texture.executions[0].expected[30] = raw[2];
    nan_texture.executions[0].expected[31] = bits(cases::detail::clamp_away_zero_inf(from_bits(raw[3])));
    out.push_back({"texture_nan_forces_gpu_vsh_decline_cpu_fallback", std::move(nan_texture),
                   false, true, true});

    const auto existing = cases::make_batch_probe_cases();
    if (existing.size() > 15) {
        out.push_back({"existing_cachetail_structural", existing[15], false,
                       existing[15].expected_decline, false});
    }
    for (size_t i = 0; i < out.size(); ++i) {
        out[i].batch.sequence = static_cast<uint64_t>(i + 1);
        cases::detail::verify_expected(out[i].batch);
        cap::detail::validate_batch(out[i].batch);
    }
    return out;
}

int fail(const std::string& message) {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    return 1;
}

}  // namespace

int run(bool parallel_pack, bool deferred) {
    std::string error;
    auto cpu_renderer = xlle::create_nv2a_opengl_renderer(error);
    if (!cpu_renderer) {
        std::printf("SKIP: CPU baseline OpenGL renderer unavailable: %s\n", error.c_str());
        return 2;
    }
    auto gpu_renderer = xlle::create_nv2a_opengl_renderer(error);
    if (!gpu_renderer) {
        std::printf("SKIP: GPU VSH OpenGL renderer unavailable: %s\n", error.c_str());
        return 2;
    }
    gpu_renderer->set_vsh_gpu_parallel_pack(parallel_pack);
    std::printf("[resident-probe] parallel_pack=%u deferred=%u\n", parallel_pack ? 1u : 0u, deferred ? 1u : 0u);

    uint32_t case_index = 0;
    uint32_t compared = 0;
    uint32_t mode1_checked = 0;
    uint32_t gpu_draw_checked = 0;
    uint32_t decline_checked = 0;
    for (auto& probe : make_probe_cases()) {
        ++case_index;
        if (probe.batch.expected_decline || probe.batch.expected_indices.empty()) {
            std::printf("[resident-probe] skip_draw name=%s reason=%s\n",
                        probe.name.c_str(), probe.batch.expected_decline ? "cpu_decline" : "empty_cpu_expansion");
            continue;
        }
        const uint32_t cpu_address = kBaseAddress + case_index * 0x20000u;
        const uint32_t gpu_address = cpu_address + 0x10000u;
        auto cpu_packet = make_packet(probe.batch, cpu_address, probe.x8, 0);
        auto gpu_packet = make_packet(probe.batch, gpu_address, probe.x8, deferred ? 3 : 2);
        if (probe.malformed_gpu_layout) {
            if (!gpu_packet.gpu_vsh || gpu_packet.gpu_vsh->batch.ops.empty())
                return fail(probe.name + ": missing GPU reference for malformed layout mutation");
            gpu_packet.gpu_vsh->batch.ops[0].words[8] = 64u;
        }
        std::vector<uint8_t> cpu_output;
        std::vector<uint8_t> gpu_output;
        error.clear();
        if (!submit_and_resolve(*cpu_renderer, cpu_packet, cpu_output, error))
            return fail(probe.name + ": CPU baseline submit failed: " + error);
        const auto before = gpu_renderer->vsh_gpu_resident_stats();
        error.clear();
        if (deferred) {
            gpu_packet.gpu_vsh->expected.clear();
            const auto expected_vertices = gpu_packet.vertices.size();
            gpu_packet.vertices.clear();
            if (!gpu_renderer->prepare_vsh_gpu_batch(*gpu_packet.gpu_vsh, error))
                return fail(probe.name + ": deferred preparation failed: " + error);
            if (!gpu_packet.gpu_vsh->prepared_token) {
                if (!probe.expect_decline) return fail(probe.name + ": unexpected preparation decline");
                gpu_packet.gpu_vsh.reset();
                gpu_packet.vertices = cpu_vertices_from_expected(probe.batch);
            } else {
                if (probe.expect_decline || gpu_packet.gpu_vsh->prepared_vertices != expected_vertices)
                    return fail(probe.name + ": incorrect deferred acceptance/count");
                if (cpu_renderer->supports_resident_generic_combiner(gpu_packet))
                    return fail(probe.name + ": another renderer accepted a foreign receipt");
            }
        }
        if (!submit_and_resolve(*gpu_renderer, gpu_packet, gpu_output, error))
            return fail(probe.name + ": GPU submit failed: " + error);
        if (deferred && gpu_packet.gpu_vsh && gpu_renderer->supports_resident_generic_combiner(gpu_packet))
            return fail(probe.name + ": consumed receipt was reusable");
        const auto after = gpu_renderer->vsh_gpu_resident_stats();
        std::string diff;
        if (!equal_bytes(cpu_output, gpu_output, diff))
            return fail(probe.name + ": CPU/GPU image mismatch: " + diff);
        if (probe.require_changed && changed_bytes(cpu_output) == 0)
            return fail(probe.name + ": CPU baseline left the initialized surface unchanged");
        if (probe.x8 && !assert_x8_alpha_repair(probe.name, cpu_output, diff))
            return fail(diff);
        if (probe.expect_decline) {
            if (after.declines <= before.declines)
                return fail(probe.name + ": expected GPU VSH decline/fallback was not counted");
            if (after.gpu_draws != before.gpu_draws)
                return fail(probe.name + ": decline case unexpectedly used GPU VBO draw");
            ++decline_checked;
        } else {
            if (deferred ? after.accepted <= before.accepted : after.verified <= before.verified)
                return fail(probe.name + ": acceptance/verification success was not counted");
            if (deferred && after.verified != before.verified)
                return fail(probe.name + ": deferred draw incorrectly claimed CPU verification");
            if (after.gpu_draws <= before.gpu_draws)
                return fail(probe.name + ": mode2 GPU VBO draw was not counted");
            ++gpu_draw_checked;
        }
        ++compared;
        if (mode1_checked == 0 && !probe.expect_decline) {
            auto mode1_renderer = xlle::create_nv2a_opengl_renderer(error);
            if (!mode1_renderer) return fail("mode1 renderer creation failed: " + error);
            mode1_renderer->set_vsh_gpu_parallel_pack(parallel_pack);
            auto mode1_packet = make_packet(probe.batch, gpu_address + 0x700000u,
                                            probe.x8, 1);
            std::vector<uint8_t> mode1_output;
            error.clear();
            const auto mode1_before = mode1_renderer->vsh_gpu_resident_stats();
            if (!submit_and_resolve(*mode1_renderer, mode1_packet, mode1_output, error))
                return fail(probe.name + ": mode1 submit failed: " + error);
            const auto mode1_after = mode1_renderer->vsh_gpu_resident_stats();
            if (!equal_bytes(cpu_output, mode1_output, diff))
                return fail(probe.name + ": mode1 image mismatch: " + diff);
            if (mode1_after.batches <= mode1_before.batches ||
                mode1_after.executions <= mode1_before.executions ||
                mode1_after.references <= mode1_before.references ||
                mode1_after.verified <= mode1_before.verified ||
                mode1_after.vertices <= mode1_before.vertices)
                return fail(probe.name + ": mode1 verification counters did not advance");
            if (mode1_after.gpu_draws != mode1_before.gpu_draws)
                return fail(probe.name + ": mode1 unexpectedly used GPU VBO draw");
            if (mode1_after.mismatches != mode1_before.mismatches ||
                mode1_after.declines != mode1_before.declines)
                return fail(probe.name + ": mode1 stats recorded mismatch or decline");
            ++mode1_checked;
        }
        std::printf("[resident-probe] ok name=%s bytes_changed=%zu x8=%u decline=%u\n",
                    probe.name.c_str(), changed_bytes(cpu_output), probe.x8 ? 1u : 0u,
                    probe.expect_decline ? 1u : 0u);
    }

    if (compared < 8) return fail("too few resident probe cases compared");
    if (mode1_checked == 0) return fail("mode1 verify-CPU-draw path was not checked");
    if (gpu_draw_checked == 0) return fail("mode2 GPU VBO path was not checked");
    if (decline_checked == 0) return fail("numeric-decline CPU fallback path was not checked");
    const auto stats = gpu_renderer->vsh_gpu_resident_stats();
    std::printf("[resident-probe] PASS compared=%u gpu_draw_cases=%u decline_cases=%u "
                "stats_batches=%llu verified=%llu gpu_draws=%llu declines=%llu mismatches=%llu\n",
                compared, gpu_draw_checked, decline_checked,
                static_cast<unsigned long long>(stats.batches),
                static_cast<unsigned long long>(stats.verified),
                static_cast<unsigned long long>(stats.gpu_draws),
                static_cast<unsigned long long>(stats.declines),
                static_cast<unsigned long long>(stats.mismatches));
    return 0;
}

int main(int argc, char** argv) {
    try {
        bool parallel_pack = false, deferred = false;
        for (int i = 1; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--parallel-pack")) parallel_pack = true;
            else if (!std::strcmp(argv[i], "--deferred")) deferred = true;
            else {
                std::fprintf(stderr, "usage: xlle_vsh_resident_probe [--parallel-pack] [--deferred]\n");
                return 2;
            }
        }
        return run(parallel_pack, deferred);
    } catch (const std::exception& ex) {
        return fail(std::string("unhandled exception: ") + ex.what());
    } catch (...) {
        return fail("unhandled non-standard exception");
    }
}

#endif
