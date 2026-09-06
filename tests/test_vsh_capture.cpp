#include "../tools/nv2a_vsh_capture_oracle.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
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

using xlle::vsh_capture::Op;
using xlle::vsh_capture::Vertex;
using xlle::vsh_capture_oracle::trace_numeric_decline;
using xlle::vsh_capture_oracle::validate_and_execute;

uint32_t bits(float value) {
    uint32_t out = 0;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

void set_word(std::array<uint32_t, 64>& regs, unsigned reg, unsigned lane,
              uint32_t word) {
    regs[reg * 4u + lane] = word;
}

void set_word(std::array<uint32_t, 768>& regs, unsigned reg, unsigned lane,
              uint32_t word) {
    regs[reg * 4u + lane] = word;
}

Op make_op(uint32_t opcode, uint32_t arity, uint32_t out_base, uint32_t out_reg,
           uint32_t mask) {
    Op op{};
    op.words[0] = opcode;
    op.words[2] = arity;
    op.words[4] = xlle::kVshBaseInput;
    op.words[5] = arity > 1 ? xlle::kVshBaseContext : xlle::kVshBaseNone;
    op.words[6] = arity > 2 ? xlle::kVshBaseTemp : xlle::kVshBaseNone;
    op.words[8] = 0;
    op.words[9] = 0;
    op.words[10] = 0;
    op.words[12] = 0xE4;
    op.words[13] = arity > 1 ? 0xE4 : 0;
    op.words[14] = arity > 2 ? 0xE4 : 0;
    op.words[16] = out_base;
    op.words[17] = xlle::kVshBaseNone;
    op.words[18] = out_reg * 4u;
    op.words[20] = mask;
    return op;
}

Op mov_input_to_output(unsigned input_reg, unsigned output_reg, uint32_t mask) {
    Op op = make_op(1, 1, xlle::kVshBaseOutput, output_reg, mask);
    op.words[8] = input_reg * 4u;
    return op;
}

Op dp4_input_context_to_temp(unsigned input_reg, unsigned context_reg,
                             unsigned temp_reg, uint32_t mask) {
    Op op = make_op(7, 2, xlle::kVshBaseTemp, temp_reg, mask);
    op.words[8] = input_reg * 4u;
    op.words[9] = context_reg * 4u;
    return op;
}

Op rsq_temp_w_to_output(unsigned temp_reg, unsigned output_reg, uint32_t mask) {
    Op op = make_op(16, 1, xlle::kVshBaseOutput, output_reg, mask);
    op.words[4] = xlle::kVshBaseTemp;
    op.words[8] = temp_reg * 4u;
    op.words[12] = 0xFF;
    return op;
}

void expect_throw(const Vertex& vertex, const char* label) {
    try {
        (void)validate_and_execute(vertex);
        std::cerr << "CHECK failed: expected throw for " << label << "\n";
        ++g_failures;
    } catch (const std::runtime_error&) {
    }
}

unsigned current_process_id() {
#ifdef _WIN32
    return static_cast<unsigned>(_getpid());
#else
    return static_cast<unsigned>(getpid());
#endif
}

std::filesystem::path temp_capture_path(const char* stem) {
    static std::atomic<unsigned> counter{0};
    return std::filesystem::temp_directory_path() /
           (std::string("xlle_vsh_capture_") + std::to_string(current_process_id()) +
            "_" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) +
            "_" + stem + ".bin");
}

void write_words(const std::filesystem::path& path, const std::vector<uint32_t>& words) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(words.data()),
               static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
}

void test_format_roundtrip_and_footer_bounds() {
    Vertex vertex;
    vertex.ordinal = 1;
    vertex.fingerprint = 0x123456789ABCDEF0ull;
    vertex.ops.push_back(mov_input_to_output(0, 0, 0xFu));
    for (unsigned lane = 0; lane < 4; ++lane)
        set_word(vertex.inputs, 0, lane, bits(float(lane + 1)));
    vertex.expected = validate_and_execute(vertex);
    vertex.post_position = {bits(1.25f), bits(-2.5f), bits(0.0f), 0x7FC00000u};

    const auto path = temp_capture_path("roundtrip");
    {
        xlle::vsh_capture::Writer writer(path.string().c_str());
        CHECK(writer.good());
        CHECK(writer.append(vertex));
        CHECK(writer.finish());
    }
    const std::vector<Vertex> loaded = xlle::vsh_capture::read(path.string().c_str());
    CHECK(loaded.size() == 1);
    CHECK(loaded[0].ordinal == vertex.ordinal);
    CHECK(loaded[0].fingerprint == vertex.fingerprint);
    CHECK(loaded[0].ops.size() == 1);
    CHECK(loaded[0].post_position == vertex.post_position);
    CHECK(validate_and_execute(loaded[0]) == vertex.expected);
    std::filesystem::remove(path);

    const auto truncated = temp_capture_path("truncated");
    write_words(truncated, {xlle::vsh_capture::kMagic, 1u, 0x01020304u, 0u,
                            xlle::vsh_capture::kRecord, 1u});
    try {
        (void)xlle::vsh_capture::read(truncated.string().c_str());
        CHECK(false);
    } catch (const std::runtime_error&) {
    }
    std::filesystem::remove(truncated);

    const auto bad_footer = temp_capture_path("footer");
    write_words(bad_footer, {xlle::vsh_capture::kMagic, 1u, 0x01020304u, 0u,
                             xlle::vsh_capture::kEnd, 1u});
    try {
        (void)xlle::vsh_capture::read(bad_footer.string().c_str());
        CHECK(false);
    } catch (const std::runtime_error&) {
    }
    std::filesystem::remove(bad_footer);
}

void test_invalid_operands_and_pairing_are_rejected() {
    Vertex vertex;
    vertex.ordinal = 1;
    vertex.ops.push_back(mov_input_to_output(0, 0, 0xFu));

    Vertex bad_opcode = vertex;
    bad_opcode.ops[0].words[0] = 20;
    expect_throw(bad_opcode, "bad opcode");

    Vertex bad_arity = vertex;
    bad_arity.ops[0].words[2] = 2;
    bad_arity.ops[0].words[5] = xlle::kVshBaseInput;
    expect_throw(bad_arity, "bad arity");

    Vertex bad_relative = vertex;
    bad_relative.ops[0].words[7] = 1;
    expect_throw(bad_relative, "relative operand");

    Vertex bad_context_write = vertex;
    bad_context_write.ops[0].words[16] = xlle::kVshBaseContext;
    bad_context_write.ops[0].words[18] = 0;
    expect_throw(bad_context_write, "context write");

    Vertex bad_address_write = vertex;
    bad_address_write.ops[0].words[16] = xlle::kVshBaseAddress;
    expect_throw(bad_address_write, "address write");

    Vertex bad_offset = vertex;
    bad_offset.ops[0].words[8] = 1;
    expect_throw(bad_offset, "unaligned source offset");

    Vertex bad_upper_bound = vertex;
    bad_upper_bound.ops[0].words[8] = 16u * 4u;
    expect_throw(bad_upper_bound, "source offset past input register file");

    Vertex bad_unused_operand = vertex;
    bad_unused_operand.ops[0].words[5] = xlle::kVshBaseInput;
    expect_throw(bad_unused_operand, "unused operand field");

    Vertex bad_mask = vertex;
    bad_mask.ops[0].words[20] = 0x10u;
    expect_throw(bad_mask, "destination mask");

    Vertex bad_reserved = vertex;
    bad_reserved.ops[0].words[11] = 99;
    expect_throw(bad_reserved, "reserved field");

    Vertex missing_pair = vertex;
    missing_pair.ops[0].words[1] = 1;
    expect_throw(missing_pair, "missing pair");

    Vertex bad_pair = vertex;
    bad_pair.ops.push_back(dp4_input_context_to_temp(0, 0, 0, 1));
    bad_pair.ops[0].words[1] = 1;
    expect_throw(bad_pair, "paired non-unary");
}

void test_paired_reads_before_stores_semantics() {
    Op mac = dp4_input_context_to_temp(1, 1, 0, 1);  // paired MAC writes r0.w = 16
    mac.words[1] = 1;
    Op ilu = rsq_temp_w_to_output(0, 0, 0xFu);       // paired ILU must read old r0.w == 4

    // The initial temp register file is zero, so make the ILU old-value read
    // visible by first writing r0.w with an independent unpaired op.
    Vertex reference;
    reference.ordinal = 1;
    reference.ops.push_back(dp4_input_context_to_temp(0, 0, 0, 1));
    reference.ops.push_back(mac);
    reference.ops.push_back(ilu);
    for (unsigned lane = 0; lane < 4; ++lane) {
        set_word(reference.inputs, 0, lane, bits(1.0f));
        set_word(reference.constants, 0, lane, bits(1.0f));
        set_word(reference.inputs, 1, lane, bits(2.0f));
        set_word(reference.constants, 1, lane, bits(2.0f));
    }
    std::array<uint32_t, 28> result = validate_and_execute(reference);
    CHECK(result[0] == bits(0.5f));
    CHECK(result[1] == bits(0.5f));
    CHECK(result[2] == bits(0.5f));
    CHECK(result[3] == bits(0.5f));
}

void test_expected_mismatch_is_not_silenced() {
    Vertex vertex;
    vertex.ordinal = 1;
    vertex.ops.push_back(mov_input_to_output(0, 0, 0xFu));
    set_word(vertex.inputs, 0, 0, bits(7.0f));
    set_word(vertex.inputs, 0, 1, bits(-0.0f));
    set_word(vertex.inputs, 0, 2, bits(3.0f));
    set_word(vertex.inputs, 0, 3, bits(4.0f));
    vertex.expected.fill(0xDEADBEEFu);
    const auto actual = validate_and_execute(vertex);
    CHECK(actual != vertex.expected);
    CHECK(actual[0] == bits(7.0f));
    CHECK(actual[1] == 0x80000000u);
    CHECK(actual[2] == bits(3.0f));
    CHECK(actual[3] == bits(4.0f));
}

void test_program_probe_cases_are_self_consistent() {
    const std::vector<Vertex> cases = xlle::vsh_capture_oracle::make_program_probe_cases();
    CHECK(cases.size() >= 19);
    for (const Vertex& vertex : cases) {
        CHECK(!vertex.ops.empty());
        const auto trace = trace_numeric_decline(vertex);
        CHECK(trace.outputs == validate_and_execute(vertex));
        CHECK(trace.outputs == vertex.expected);
    }
    // Independent semantic expectations, not just a repeated oracle call.
    CHECK(cases[0].expected[3] == bits(0.5f));       // paired RSQ reads pre-MAC r0.w
    CHECK(cases[1].expected[0] == bits(3.0f));       // output read-after-write
    CHECK(cases[1].expected[3] == bits(4.0f));       // XYZ mask preserves W
    CHECK(cases[2].expected[4] == bits(-2.0f));      // reversed W, negated, times .5
    CHECK(cases[2].expected[5] == 0u);               // masked-out diffuse Y stays initialized
    CHECK(cases[3].expected[4] == 0xdf800000u);      // RCC(-0) clamps negative infinity
    CHECK(cases[4].expected[4] == bits(9.0f));       // dual destination slot 0 writes o3.x
    CHECK(cases[4].expected[9] == bits(10.0f));      // dual destination slot 1 writes o4.y
    CHECK(cases[5].expected[0] == bits(0.25f));      // paired ILU writes after paired MAC
    CHECK(cases[6].expected[4] == 0u);               // MIN(-0,+0) returns operand 1
    CHECK(cases[7].expected[4] == 0x80000000u);      // MIN(+0,-0) returns operand 1
    CHECK(cases[8].expected[4] == 0u);               // MAX(-0,+0) returns operand 1
    CHECK(cases[9].expected[4] == 0x80000000u);      // MAX(+0,-0) returns operand 1
    CHECK(cases[10].expected[4] == bits(1.0f));      // SLT(-0,+0)
    CHECK(cases[11].expected[4] == 0u);              // SLT(+0,-0)
    CHECK(cases[12].expected[4] == 0u);              // SGE(-0,+0)
    CHECK(cases[13].expected[4] == bits(1.0f));      // SGE(+0,-0)
    CHECK(cases[14].expected[4] == bits(64.0f));     // DPH uses b.w as homogeneous term
    CHECK(cases[15].expected[4] == bits(1.0f));      // DST.x
    CHECK(cases[15].expected[5] == bits(15.0f));     // DST.y = a.y * b.y
    CHECK(cases[15].expected[6] == bits(7.0f));      // DST.z = a.z
    CHECK(cases[15].expected[7] == bits(19.0f));     // DST.w = b.w
    CHECK(cases[16].expected[4] == 0u);              // MAD fixes product overflow before add
    CHECK(!trace_numeric_decline(cases[16]).numeric_decline);
    CHECK(trace_numeric_decline(cases[17]).numeric_decline); // +Inf + -Inf result NaN
    CHECK(cases[18].ops[0].words[0] == 19u);        // explicit LIT program-decline record
    CHECK(cases[18].expected[4] == bits(1.0f));
    CHECK(cases[18].expected[5] == bits(2.0f));
    CHECK(cases[18].expected[6] == bits(64.0f));
    CHECK(cases[18].expected[7] == bits(1.0f));
}

void test_trace_reports_input_and_result_nan_declines() {
    Vertex input_nan;
    input_nan.ordinal = 1;
    input_nan.ops.push_back(mov_input_to_output(0, 0, 0xFu));
    set_word(input_nan.inputs, 0, 0, 0x7FC00000u);
    const auto input_trace = trace_numeric_decline(input_nan);
    CHECK(input_trace.numeric_decline);
    CHECK(input_trace.outputs == validate_and_execute(input_nan));

    Vertex result_nan;
    result_nan.ordinal = 2;
    result_nan.ops.push_back(make_op(3, 2, xlle::kVshBaseOutput, 0, 0xFu));
    set_word(result_nan.inputs, 0, 0, 0x7f800000u);
    set_word(result_nan.constants, 0, 0, 0xff800000u);
    const auto result_trace = trace_numeric_decline(result_nan);
    CHECK(result_trace.numeric_decline);
    CHECK(result_trace.outputs == validate_and_execute(result_nan));
}

}  // namespace

int main() {
    test_format_roundtrip_and_footer_bounds();
    test_invalid_operands_and_pairing_are_rejected();
    test_paired_reads_before_stores_semantics();
    test_expected_mismatch_is_not_silenced();
    test_program_probe_cases_are_self_consistent();
    test_trace_reports_input_and_result_nan_declines();
    if (g_failures) {
        std::cerr << g_failures << " failures\n";
        return 1;
    }
    return 0;
}
