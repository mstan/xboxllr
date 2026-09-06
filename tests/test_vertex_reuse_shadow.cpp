#include "machine/nv2a_vertex_reuse_shadow.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " \
                      << #cond << "\n";                                            \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

using Shadow = xlle::Nv2aVertexReuseShadow;
using ProgramWords = Shadow::ProgramWords;
using Constants = Shadow::Constants;
using Input = std::array<float, Shadow::kInputFloats>;

void set_bits(float& value, uint32_t bits) {
    std::memcpy(&value, &bits, sizeof(bits));
}

ProgramWords make_program(uint32_t salt = 0) {
    ProgramWords program{};
    program[0][0] = 0x10000000u ^ salt;
    program[1][2] = 0x20000000u + salt;
    program[135][3] = 0xF0000000u | salt;
    return program;
}

Constants make_constants(uint32_t salt = 0) {
    Constants constants{};
    for (uint32_t i = 0; i < constants.size(); ++i) {
        set_bits(constants[i][0], 0x3F800000u + i + salt);
        set_bits(constants[i][1], 0x40000000u + i + salt);
        set_bits(constants[i][2], 0x40400000u + i + salt);
        set_bits(constants[i][3], 0x40800000u + i + salt);
    }
    return constants;
}

Input make_input(uint32_t salt = 0) {
    Input input{};
    for (uint32_t i = 0; i < input.size(); ++i)
        set_bits(input[i], 0x3F000000u + i + salt);
    return input;
}

void test_exact_state_and_hit_classification() {
    Shadow shadow;
    const ProgramWords program = make_program();
    const Constants constants = make_constants();
    const Input input = make_input();

    shadow.observe(input.data());
    CHECK(shadow.stats().seen == 1);
    CHECK(shadow.stats().misses == 1);

    shadow.begin_batch(program, 0, constants, true);
    shadow.observe(input.data());
    shadow.observe(input.data());
    shadow.begin_batch(program, 0, constants, true);
    shadow.observe(input.data());

    const auto& stats = shadow.stats();
    CHECK(stats.batches == 2);
    CHECK(stats.state_changes == 1);
    CHECK(stats.seen == 4);
    CHECK(stats.misses == 2);
    CHECK(stats.within_hits == 1);
    CHECK(stats.cross_hits == 1);
    CHECK(stats.collisions == 0);
    CHECK(stats.exact_compares == 2);
    CHECK(stats.compared_bytes_requested == 2 * Shadow::kInputBytes);
}

void test_state_mutations_invalidate_by_generation() {
    Shadow shadow;
    const Input input = make_input();
    const ProgramWords program = make_program();
    const Constants constants = make_constants();

    shadow.begin_batch(program, 0, constants, true);
    shadow.observe(input.data());

    shadow.begin_batch(program, 0, constants, true);
    shadow.observe(input.data());

    Constants changed_constants = constants;
    set_bits(changed_constants[191][3], 0x7FC00001u);
    shadow.begin_batch(program, 0, changed_constants, true);
    shadow.observe(input.data());
    shadow.begin_batch(program, 0, changed_constants, true);
    shadow.observe(input.data());

    ProgramWords changed_program = program;
    changed_program[135][3] ^= 0x01020304u;
    shadow.begin_batch(changed_program, 0, changed_constants, true);
    shadow.observe(input.data());

    shadow.begin_batch(changed_program, 1, changed_constants, true);
    shadow.observe(input.data());

    shadow.begin_batch(changed_program, 1, changed_constants, false);
    shadow.observe(input.data());

    const auto& stats = shadow.stats();
    CHECK(stats.batches == 7);
    CHECK(stats.state_changes == 5);
    CHECK(stats.seen == 7);
    CHECK(stats.cross_hits == 2);
    CHECK(stats.within_hits == 0);
    CHECK(stats.misses == 5);
    CHECK(stats.collisions == 0);
}

void test_unselected_input_word_still_misses_after_exact_compare() {
    Shadow shadow;
    const ProgramWords program = make_program();
    const Constants constants = make_constants();
    Input input = make_input();
    Input changed = input;
    set_bits(changed[63], 0x12345678u);

    CHECK(Shadow::selector_slot_for_test(input.data()) ==
          Shadow::selector_slot_for_test(changed.data()));

    shadow.begin_batch(program, 0, constants, true);
    shadow.observe(input.data());
    shadow.observe(changed.data());

    const auto& stats = shadow.stats();
    CHECK(stats.seen == 2);
    CHECK(stats.misses == 2);
    CHECK(stats.within_hits == 0);
    CHECK(stats.cross_hits == 0);
    CHECK(stats.collisions == 1);
    CHECK(stats.exact_compares == 1);
    CHECK(stats.compared_bytes_requested == Shadow::kInputBytes);
}

void test_last_byte_signed_zero_and_nan_payload_are_token_exact() {
    const ProgramWords program = make_program();
    const Constants constants = make_constants();

    {
        Shadow shadow;
        Input input = make_input();
        Input changed = input;
        auto* changed_bytes = reinterpret_cast<uint8_t*>(changed.data());
        changed_bytes[Shadow::kInputBytes - 1] ^= 0x80u;
        CHECK(Shadow::selector_slot_for_test(input.data()) ==
              Shadow::selector_slot_for_test(changed.data()));

        shadow.begin_batch(program, 0, constants, true);
        shadow.observe(input.data());
        shadow.observe(changed.data());
        CHECK(shadow.stats().misses == 2);
        CHECK(shadow.stats().collisions == 1);
    }

    {
        Shadow shadow;
        Input positive_zero = make_input();
        Input negative_zero = positive_zero;
        set_bits(positive_zero[63], 0x00000000u);
        set_bits(negative_zero[63], 0x80000000u);
        CHECK(Shadow::selector_slot_for_test(positive_zero.data()) ==
              Shadow::selector_slot_for_test(negative_zero.data()));

        shadow.begin_batch(program, 0, constants, true);
        shadow.observe(positive_zero.data());
        shadow.observe(negative_zero.data());
        CHECK(shadow.stats().misses == 2);
        CHECK(shadow.stats().collisions == 1);
    }

    {
        Shadow shadow;
        Input nan_a = make_input();
        Input nan_b = nan_a;
        set_bits(nan_a[63], 0x7FC00001u);
        set_bits(nan_b[63], 0x7FC00002u);
        CHECK(Shadow::selector_slot_for_test(nan_a.data()) ==
              Shadow::selector_slot_for_test(nan_b.data()));

        shadow.begin_batch(program, 0, constants, true);
        shadow.observe(nan_a.data());
        shadow.observe(nan_b.data());
        CHECK(shadow.stats().misses == 2);
        CHECK(shadow.stats().collisions == 1);
    }
}

void test_collision_replacement_and_source_ownership() {
    const ProgramWords program = make_program();
    const Constants constants = make_constants();

    {
        Shadow shadow;
        Input first = make_input();
        Input second = first;
        set_bits(second[62], 0x11111111u);
        CHECK(Shadow::selector_slot_for_test(first.data()) ==
              Shadow::selector_slot_for_test(second.data()));

        shadow.begin_batch(program, 0, constants, true);
        shadow.observe(first.data());
        shadow.observe(second.data());
        shadow.observe(second.data());

        const auto& stats = shadow.stats();
        CHECK(stats.misses == 2);
        CHECK(stats.collisions == 1);
        CHECK(stats.within_hits == 1);
    }

    {
        Shadow shadow;
        Input source = make_input();
        Input original = source;

        shadow.begin_batch(program, 0, constants, true);
        shadow.observe(source.data());
        set_bits(source[63], 0x22222222u);
        shadow.observe(original.data());

        const auto& stats = shadow.stats();
        CHECK(stats.misses == 1);
        CHECK(stats.within_hits == 1);
        CHECK(stats.collisions == 0);
    }
}

void test_report_sizes_are_explicit() {
    CHECK(Shadow::kSlots == 1024);
    CHECK(Shadow::kInputBytes == 256);
    CHECK(Shadow::entry_size_for_report() >= Shadow::kInputBytes + 2 * sizeof(uint64_t));
    CHECK(Shadow::retained_bytes_for_report() ==
          Shadow::kSlots * Shadow::entry_size_for_report());
}

}  // namespace

int main() {
    test_exact_state_and_hit_classification();
    test_state_mutations_invalidate_by_generation();
    test_unselected_input_word_still_misses_after_exact_compare();
    test_last_byte_signed_zero_and_nan_payload_are_token_exact();
    test_collision_replacement_and_source_ownership();
    test_report_sizes_are_explicit();

    if (g_failures) {
        std::cerr << "test_vertex_reuse_shadow: " << g_failures
                  << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "test_vertex_reuse_shadow: all checks passed\n";
    return EXIT_SUCCESS;
}
