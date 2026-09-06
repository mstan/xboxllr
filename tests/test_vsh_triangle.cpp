#include "../src/machine/nv2a_vsh_batch_capture.hpp"
#include "../tools/nv2a_vsh_batch_probe_cases.hpp"
#include "../tools/nv2a_vsh_triangle_oracle.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

int g_failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__    \
                      << ": " #expr << "\n";                                  \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

using xlle::vsh_batch_capture::Batch;
using xlle::vsh_batch_capture::Execution;

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

Execution make_execution(float x, float y, float z = 0.0f, float w = 1.0f,
                         float r = 1.0f, float g = 0.5f, float b = 0.25f,
                         float a = 1.0f) {
    Execution execution;
    execution.expected[4] = bits(r);
    execution.expected[5] = bits(g);
    execution.expected[6] = bits(b);
    execution.expected[7] = bits(a);
    execution.expected[28] = bits(x);
    execution.expected[29] = bits(y);
    execution.expected[30] = bits(z);
    execution.expected[31] = bits(w);
    return execution;
}

Batch make_uncached_batch(uint32_t mode, std::vector<Execution> executions) {
    Batch batch;
    batch.sequence = 1;
    batch.fingerprint = 0xA55AA55Au;
    batch.primitive_mode = mode;
    batch.cache_enabled = false;
    batch.executions = std::move(executions);
    batch.source_indices.reserve(batch.executions.size());
    batch.references.reserve(batch.executions.size());
    for (uint32_t i = 0; i < batch.executions.size(); ++i) {
        batch.source_indices.push_back(i);
        batch.references.push_back(i);
    }
    return batch;
}

unsigned current_process_id() {
#ifdef _WIN32
    return static_cast<unsigned>(_getpid());
#else
    return static_cast<unsigned>(getpid());
#endif
}

std::filesystem::path temp_path(const char* stem) {
    static std::atomic<unsigned> counter{0};
    return std::filesystem::temp_directory_path() /
           (std::string("xlle_vsh_batch_capture_") +
            std::to_string(current_process_id()) + "_" +
            std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) +
            "_" + stem + ".bin");
}

void write_words(const std::filesystem::path& path, const std::vector<uint32_t>& words) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(words.data()),
               static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
}

void expect_read_throw(const std::filesystem::path& path, const char* label) {
    try {
        (void)xlle::vsh_batch_capture::read(path.string().c_str());
        std::cerr << "CHECK failed: expected read throw for " << label << "\n";
        ++g_failures;
    } catch (const std::runtime_error&) {
    }
}

std::pair<uint32_t, uint32_t> find_cache_collision() {
    std::array<uint32_t, xlle::vsh_batch_capture::kCacheEntries> first{};
    std::array<uint8_t, xlle::vsh_batch_capture::kCacheEntries> seen{};
    for (uint32_t index = 0; index < 100000; ++index) {
        const uint32_t slot = xlle::vsh_batch_capture::detail::cache_slot(index);
        if (seen[slot] && first[slot] != index) return {first[slot], index};
        seen[slot] = 1;
        first[slot] = index;
    }
    return {0, 0};
}

void test_codec_roundtrip_and_bounds() {
    Batch batch = make_uncached_batch(
        5, {make_execution(0.0f, 0.0f), make_execution(1.0f, 0.0f),
            make_execution(0.0f, 1.0f)});
    batch.sequence = 7;
    batch.fingerprint = 0x123456789abcdef0ull;
    batch.large_target = true;
    batch.cull_enabled = true;
    batch.cull_front = false;
    batch.expected_decline = false;
    batch.expected_indices = {0, 1, 2};
    batch.constants[17] = 0xCAFEBABEu;
    xlle::vsh_capture::Op op{};
    op.words[0] = 1;
    op.words[23] = 99;
    batch.ops.push_back(op);

    const auto path = temp_path("roundtrip");
    {
        xlle::vsh_batch_capture::Writer writer(path.string().c_str());
        CHECK(writer.good());
        CHECK(writer.append(batch));
        CHECK(writer.count() == 1);
        CHECK(writer.finish());
    }
    const std::vector<Batch> loaded =
        xlle::vsh_batch_capture::read(path.string().c_str());
    CHECK(loaded.size() == 1);
    CHECK(loaded[0].sequence == batch.sequence);
    CHECK(loaded[0].fingerprint == batch.fingerprint);
    CHECK(loaded[0].primitive_mode == 5);
    CHECK(loaded[0].large_target);
    CHECK(loaded[0].cull_enabled);
    CHECK(!loaded[0].cull_front);
    CHECK(!loaded[0].cache_enabled);
    CHECK(loaded[0].constants[17] == 0xCAFEBABEu);
    CHECK(loaded[0].ops.size() == 1);
    CHECK(loaded[0].ops[0].words[23] == 99);
    CHECK(loaded[0].expected_indices == std::vector<uint32_t>({0, 1, 2}));
    CHECK(from_bits(loaded[0].executions[2].expected[29]) == 1.0f);
    std::filesystem::remove(path);

    const auto truncated = temp_path("truncated");
    write_words(truncated, {xlle::vsh_batch_capture::kMagic,
                            xlle::vsh_batch_capture::kVersion,
                            xlle::vsh_batch_capture::kEndian, 0u,
                            xlle::vsh_batch_capture::kBatchRecord, 1u});
    expect_read_throw(truncated, "truncated batch");
    std::filesystem::remove(truncated);

    const auto bad_footer = temp_path("bad_footer");
    write_words(bad_footer, {xlle::vsh_batch_capture::kMagic,
                             xlle::vsh_batch_capture::kVersion,
                             xlle::vsh_batch_capture::kEndian, 0u,
                             xlle::vsh_batch_capture::kEnd, 1u});
    expect_read_throw(bad_footer, "bad footer");
    std::filesystem::remove(bad_footer);
}

void test_submitted_reference_mapping_validation() {
    Batch sequential = make_uncached_batch(
        5, {make_execution(0.0f, 0.0f), make_execution(1.0f, 0.0f),
            make_execution(0.0f, 1.0f)});
    xlle::vsh_batch_capture::detail::validate_mapping(sequential);

    Batch indexed_without_cache = sequential;
    indexed_without_cache.source_indices = {42, 17, 42};
    xlle::vsh_batch_capture::detail::validate_mapping(indexed_without_cache);

    Batch bad_uncached = sequential;
    bad_uncached.references[1] = 0;
    const auto bad_uncached_path = temp_path("bad_uncached");
    {
        xlle::vsh_batch_capture::Writer writer(bad_uncached_path.string().c_str());
        CHECK(!writer.append(bad_uncached));
    }
    std::filesystem::remove(bad_uncached_path);

    Batch repeated;
    repeated.sequence = 2;
    repeated.primitive_mode = 5;
    repeated.cache_enabled = true;
    repeated.executions = {make_execution(0.0f, 0.0f), make_execution(1.0f, 0.0f)};
    repeated.source_indices = {7, 8, 7};
    repeated.references = {0, 1, 0};
    xlle::vsh_batch_capture::detail::validate_mapping(repeated);

    const auto collision = find_cache_collision();
    CHECK(collision.first != collision.second);
    Batch colliding;
    colliding.sequence = 3;
    colliding.primitive_mode = 5;
    colliding.cache_enabled = true;
    colliding.executions = {make_execution(0.0f, 0.0f), make_execution(1.0f, 0.0f),
                            make_execution(0.0f, 1.0f)};
    colliding.source_indices = {collision.first, collision.second, collision.first};
    colliding.references = {0, 1, 2};
    xlle::vsh_batch_capture::detail::validate_mapping(colliding);

    Batch bad_collision = colliding;
    bad_collision.references[2] = 0;
    const auto bad_collision_path = temp_path("bad_collision");
    {
        xlle::vsh_batch_capture::Writer writer(bad_collision_path.string().c_str());
        CHECK(!writer.append(bad_collision));
    }
    std::filesystem::remove(bad_collision_path);
}

void test_sequence_and_expected_index_relationships() {
    Batch base = make_uncached_batch(
        5, {make_execution(0.0f, 0.0f), make_execution(1.0f, 0.0f),
            make_execution(0.0f, 1.0f)});
    base.expected_indices = {0, 1, 2};
    xlle::vsh_batch_capture::detail::validate_batch(base);

    Batch zero_sequence = base;
    zero_sequence.sequence = 0;
    const auto zero_sequence_path = temp_path("zero_sequence");
    {
        xlle::vsh_batch_capture::Writer writer(zero_sequence_path.string().c_str());
        CHECK(!writer.append(zero_sequence));
    }
    std::filesystem::remove(zero_sequence_path);

    const auto nonincreasing_sequence_path = temp_path("nonincreasing_sequence");
    {
        xlle::vsh_batch_capture::Writer writer(nonincreasing_sequence_path.string().c_str());
        CHECK(writer.append(base));
        Batch same_sequence = base;
        CHECK(!writer.append(same_sequence));
    }
    std::filesystem::remove(nonincreasing_sequence_path);

    Batch declined_with_indices = base;
    declined_with_indices.expected_decline = true;
    const auto declined_with_indices_path = temp_path("declined_with_indices");
    {
        xlle::vsh_batch_capture::Writer writer(declined_with_indices_path.string().c_str());
        CHECK(!writer.append(declined_with_indices));
    }
    std::filesystem::remove(declined_with_indices_path);

    Batch non_triangle_indices = base;
    non_triangle_indices.expected_indices = {0, 1};
    const auto non_triangle_indices_path = temp_path("non_triangle_indices");
    {
        xlle::vsh_batch_capture::Writer writer(non_triangle_indices_path.string().c_str());
        CHECK(!writer.append(non_triangle_indices));
    }
    std::filesystem::remove(non_triangle_indices_path);

    Batch list_too_many = make_uncached_batch(
        5, {make_execution(0.0f, 0.0f), make_execution(1.0f, 0.0f),
            make_execution(0.0f, 1.0f), make_execution(2.0f, 0.0f)});
    list_too_many.expected_indices = {0, 1, 2, 1, 2, 3};
    const auto list_too_many_path = temp_path("list_too_many");
    {
        xlle::vsh_batch_capture::Writer writer(list_too_many_path.string().c_str());
        CHECK(!writer.append(list_too_many));
    }
    std::filesystem::remove(list_too_many_path);

    Batch strip_too_many = make_uncached_batch(
        6, {make_execution(0.0f, 0.0f), make_execution(1.0f, 0.0f),
            make_execution(0.0f, 1.0f)});
    strip_too_many.expected_indices = {0, 1, 2, 0, 2, 1};
    const auto strip_too_many_path = temp_path("strip_too_many");
    {
        xlle::vsh_batch_capture::Writer writer(strip_too_many_path.string().c_str());
        CHECK(!writer.append(strip_too_many));
    }
    std::filesystem::remove(strip_too_many_path);
}

void test_triangle_modes_have_fixed_output_indices() {
    Batch list = make_uncached_batch(
        5, {make_execution(0.0f, 0.0f), make_execution(1.0f, 0.0f),
            make_execution(0.0f, 1.0f), make_execution(2.0f, 0.0f),
            make_execution(3.0f, 0.0f), make_execution(2.0f, 1.0f)});
    CHECK(!xlle::vsh_triangle_oracle::expand_resident_primary(list).declined);
    CHECK(xlle::vsh_triangle_oracle::expand_resident_primary(list).indices ==
          std::vector<uint32_t>({0, 1, 2, 3, 4, 5}));

    Batch strip = make_uncached_batch(
        6, {make_execution(0.0f, 0.0f), make_execution(1.0f, 0.0f),
            make_execution(0.0f, 1.0f), make_execution(1.0f, 1.0f)});
    CHECK(xlle::vsh_triangle_oracle::expand_resident_primary(strip).indices ==
          std::vector<uint32_t>({0, 1, 2, 2, 1, 3}));

    Batch fan = make_uncached_batch(
        7, {make_execution(0.0f, 0.0f), make_execution(1.0f, 0.0f),
            make_execution(0.0f, 1.0f), make_execution(-1.0f, 0.0f)});
    CHECK(xlle::vsh_triangle_oracle::expand_resident_primary(fan).indices ==
          std::vector<uint32_t>({0, 1, 2, 0, 2, 3}));
}

void test_winding_cull_and_large_target_paths() {
    Batch negative = make_uncached_batch(
        5, {make_execution(0.0f, 0.0f), make_execution(0.0f, 1.0f),
            make_execution(1.0f, 0.0f)});
    CHECK(xlle::vsh_triangle_oracle::expand_resident_primary(negative).indices ==
          std::vector<uint32_t>({0, 2, 1}));

    Batch back_cull_empty = negative;
    back_cull_empty.cull_enabled = true;
    back_cull_empty.cull_front = false;
    auto back = xlle::vsh_triangle_oracle::expand_resident_primary(back_cull_empty);
    CHECK(!back.declined);
    CHECK(back.indices.empty());

    Batch front_cull_negative = negative;
    front_cull_negative.cull_enabled = true;
    front_cull_negative.cull_front = true;
    CHECK(xlle::vsh_triangle_oracle::expand_resident_primary(front_cull_negative).indices ==
          std::vector<uint32_t>({0, 2, 1}));

    Batch front_cull_positive = make_uncached_batch(
        5, {make_execution(0.0f, 0.0f), make_execution(1.0f, 0.0f),
            make_execution(0.0f, 1.0f)});
    front_cull_positive.cull_enabled = true;
    front_cull_positive.cull_front = true;
    auto front = xlle::vsh_triangle_oracle::expand_resident_primary(front_cull_positive);
    CHECK(!front.declined);
    CHECK(front.indices.empty());

    Batch large = negative;
    large.large_target = true;
    CHECK(xlle::vsh_triangle_oracle::expand_resident_primary(large).indices ==
          std::vector<uint32_t>({0, 2, 1}));
}

void test_decline_and_empty_cases() {
    Batch zero_w = make_uncached_batch(
        5, {make_execution(0.0f, 0.0f, 0.0f, 0.0f), make_execution(1.0f, 0.0f),
            make_execution(0.0f, 1.0f)});
    auto zero = xlle::vsh_triangle_oracle::expand_resident_primary(zero_w);
    CHECK(zero.declined);
    CHECK(zero.indices.empty());

    Batch all_behind = make_uncached_batch(
        5, {make_execution(0.0f, 0.0f, 0.0f, -1.0f),
            make_execution(1.0f, 0.0f, 0.0f, -2.0f),
            make_execution(0.0f, 1.0f, 0.0f, -3.0f)});
    auto behind = xlle::vsh_triangle_oracle::expand_resident_primary(all_behind);
    CHECK(!behind.declined);
    CHECK(behind.indices.empty());

    Batch cross_w = all_behind;
    cross_w.executions[2].expected[31] = bits(1.0f);
    auto cross = xlle::vsh_triangle_oracle::expand_resident_primary(cross_w);
    CHECK(cross.declined);
    CHECK(cross.indices.empty());

    Batch nan_position = make_uncached_batch(
        5, {make_execution(std::numeric_limits<float>::quiet_NaN(), 0.0f),
            make_execution(1.0f, 0.0f), make_execution(0.0f, 1.0f)});
    CHECK(xlle::vsh_triangle_oracle::expand_resident_primary(nan_position).declined);

    Batch nan_diffuse = make_uncached_batch(
        5, {make_execution(0.0f, 0.0f, 0.0f, 1.0f,
                           std::numeric_limits<float>::quiet_NaN()),
            make_execution(1.0f, 0.0f), make_execution(0.0f, 1.0f)});
    CHECK(xlle::vsh_triangle_oracle::expand_resident_primary(nan_diffuse).declined);
}

void test_fixed_round_bounds() {
    Batch at_bound = make_uncached_batch(
        5, {make_execution(62500.0f, 0.0f), make_execution(62500.0f, 1.0f),
            make_execution(62499.0f, 0.0f)});
    CHECK(!xlle::vsh_triangle_oracle::expand_resident_primary(at_bound).declined);
    CHECK(xlle::vsh_triangle_oracle::expand_resident_primary(at_bound).indices ==
          std::vector<uint32_t>({0, 1, 2}));

    Batch over_bound = at_bound;
    over_bound.executions[0].expected[28] = bits(62500.1f);
    CHECK(xlle::vsh_triangle_oracle::expand_resident_primary(over_bound).declined);

    Batch large_ignores_fixed_bound = over_bound;
    large_ignores_fixed_bound.large_target = true;
    CHECK(!xlle::vsh_triangle_oracle::expand_resident_primary(large_ignores_fixed_bound).declined);
}

void test_generated_batch_probe_cases_are_self_consistent() {
    const auto cases = xlle::vsh_shadow_probe::make_batch_probe_cases();
    CHECK(!cases.empty());
    CHECK(cases.size() <= xlle::vsh_batch_capture::kMaxBatches);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        CHECK(cases[i].sequence == i + 1);
        xlle::vsh_batch_capture::detail::validate_batch(cases[i]);
        const auto expanded =
            xlle::vsh_triangle_oracle::expand_resident_primary(cases[i]);
        CHECK(expanded.declined == cases[i].expected_decline);
        CHECK(expanded.indices == cases[i].expected_indices);
    }
}

}  // namespace

int main() {
    test_codec_roundtrip_and_bounds();
    test_submitted_reference_mapping_validation();
    test_sequence_and_expected_index_relationships();
    test_triangle_modes_have_fixed_output_indices();
    test_winding_cull_and_large_target_paths();
    test_decline_and_empty_cases();
    test_fixed_round_bounds();
    test_generated_batch_probe_cases_are_self_consistent();
    if (g_failures) {
        std::cerr << g_failures << " test_vsh_triangle checks failed\n";
        return 1;
    }
    std::cout << "test_vsh_triangle passed\n";
    return 0;
}
