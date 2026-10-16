#include "machine/nv2a_vsh_dependencies.hpp"

#include <array>
#include <cstdio>
#include <initializer_list>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); ++failures; } } while (false)

Nv2aVshInput input(Nv2aVshRegisterType type, unsigned index,
                   std::array<uint8_t, 4> swizzle = {0, 1, 2, 3}) {
    Nv2aVshInput in{};
    in.type = type; in.index = index;
    for (unsigned i = 0; i < 4; ++i) in.swizzle[i] = swizzle[i];
    return in;
}
Nv2aVshOutput output(Nv2aVshRegisterType type, unsigned index, unsigned mask) {
    Nv2aVshOutput out{};
    out.type = type; out.index = index; out.writemask = Nv2aVshWritemask(mask);
    return out;
}
Nv2aVshOperation op(Nv2aVshOpcode code, Nv2aVshOutput dst,
                     std::initializer_list<Nv2aVshInput> inputs) {
    Nv2aVshOperation result{};
    result.opcode = code; result.outputs[0] = dst;
    unsigned i = 0;
    for (const auto& in : inputs) result.inputs[i++] = in;
    return result;
}
std::array<uint8_t, 13> want(unsigned reg, uint8_t lanes) {
    std::array<uint8_t, 13> masks{}; masks[reg] = lanes; return masks;
}
template<size_t N>
xlle::VshDependencies analyze(const std::array<Nv2aVshStep, N>& program,
                               const std::array<uint8_t, 13>& observable) {
    xlle::VshDependencies result;
    const char* error = nullptr;
    CHECK(xlle::vsh_dependencies(program.data(), program.size(), observable, result, error));
    CHECK(!error);
    return result;
}

void dead_fog_and_scalar_mask() {
    std::array<Nv2aVshStep, 2> p{};
    p[0].mac = op(NV2AOP_MOV, output(NV2ART_OUTPUT, 0, 15),
                  {input(NV2ART_INPUT, 0)});
    p[1].mac = op(NV2AOP_MAD, output(NV2ART_OUTPUT, 5, 8),
                  {input(NV2ART_INPUT, 2, {2,2,2,2}),
                   input(NV2ART_CONTEXT, 137, {0,0,0,0}),
                   input(NV2ART_CONTEXT, 137, {1,1,1,1})});
    p[1].is_final = true;
    auto d = analyze(p, want(0, 15));
    CHECK(d.inputs[0] == 15 && !d.inputs[2] && !d.constants[137] && d.steps == 2);
    d = analyze(p, want(5, 1));
    CHECK(!d.inputs[0] && d.inputs[2] == 4 && d.constants[137] == 3);

    // The maintained fog patch stores the first MASKED result lane to o5.x.
    // Mask YW must use result Y, not result X or all vector lanes.
    p[1].mac = op(NV2AOP_MOV, output(NV2ART_OUTPUT, 5, 5),
                  {input(NV2ART_INPUT, 3, {1,2,3,0})});
    d = analyze(p, want(5, 1));
    CHECK(d.inputs[3] == 4 && !d.constants[137]);
}

void partial_stores_swizzles_and_two_destinations() {
    std::array<Nv2aVshStep, 3> p{};
    p[0].mac = op(NV2AOP_MOV, output(NV2ART_TEMPORARY, 0, 15),
                  {input(NV2ART_INPUT, 1)});
    p[1].mac = op(NV2AOP_MOV, output(NV2ART_TEMPORARY, 0, 8),
                  {input(NV2ART_CONTEXT, 5, {1,1,1,1})});
    p[2].mac = op(NV2AOP_MUL, output(NV2ART_OUTPUT, 0, 12),
                  {input(NV2ART_TEMPORARY, 0), input(NV2ART_CONTEXT, 6, {3,3,3,3})});
    p[2].mac.inputs[1].is_negated = true;
    p[2].is_final = true;
    auto d = analyze(p, want(0, 3));
    CHECK(d.inputs[1] == 2 && d.constants[5] == 2 && d.constants[6] == 8);
    CHECK(!d.initial_temps[0] && !d.initial_outputs[0]);

    // A later overwrite kills the prior source even with a dual destination.
    p[1].mac.outputs[1] = output(NV2ART_OUTPUT, 3, 1);
    d = analyze(p, want(3, 8));
    CHECK(d.constants[5] == 2 && !d.inputs[1] && !d.constants[6]);
}

void parallel_reads_and_last_store_priority() {
    std::array<Nv2aVshStep, 3> p{};
    p[0].mac = op(NV2AOP_MOV, output(NV2ART_TEMPORARY, 0, 8),
                  {input(NV2ART_INPUT, 7, {2,2,2,2})});
    p[1].mac = op(NV2AOP_MOV, output(NV2ART_TEMPORARY, 0, 8),
                  {input(NV2ART_CONTEXT, 1, {1,1,1,1})});
    p[1].ilu = op(NV2AOP_RCP, output(NV2ART_TEMPORARY, 1, 8),
                  {input(NV2ART_TEMPORARY, 0)});
    p[2].mac = op(NV2AOP_MOV, output(NV2ART_OUTPUT, 3, 1),
                  {input(NV2ART_TEMPORARY, 1, {0,0,0,0})});
    p[2].is_final = true;
    auto d = analyze(p, want(3, 8));
    // ILU reads OLD r0.x: its MAC neighbor cannot supply that dependency.
    CHECK(d.inputs[7] == 4 && !d.constants[1] && !d.initial_temps[0]);

    p[1].mac = op(NV2AOP_MOV, output(NV2ART_TEMPORARY, 1, 8),
                  {input(NV2ART_CONTEXT, 4)});
    p[1].ilu = op(NV2AOP_MOV, output(NV2ART_TEMPORARY, 1, 8),
                  {input(NV2ART_INPUT, 8, {2,2,2,2})});
    d = analyze(p, want(3, 8));
    CHECK(d.inputs[8] == 4 && !d.constants[4] && !d.inputs[7]);

    // RCP uses operand X regardless of which replicated result is requested.
    p[1].ilu = op(NV2AOP_RCP, output(NV2ART_OUTPUT, 4, 1),
                  {input(NV2ART_INPUT, 9, {2,0,1,3})});
    d = analyze(p, want(4, 8));
    CHECK(d.inputs[9] == 4 && !d.constants[4]);

    // ILU MOV keeps vector swizzles; unlike RCP, result W uses operand W.
    p[1].ilu.opcode = NV2AOP_MOV;
    d = analyze(p, want(4, 8));
    CHECK(d.inputs[9] == 8 && !d.constants[4]);
}

void r12_alias_and_initial_lanes() {
    std::array<Nv2aVshStep, 2> p{};
    p[0].mac = op(NV2AOP_MOV, output(NV2ART_OUTPUT, 0, 2),
                  {input(NV2ART_CONTEXT, 9, {1,1,1,1})});
    p[1].mac = op(NV2AOP_ADD, output(NV2ART_OUTPUT, 3, 8),
                  {input(NV2ART_TEMPORARY, 12, {2,2,2,2}),
                   input(NV2ART_TEMPORARY, 2, {3,3,3,3})});
    p[1].is_final = true;
    auto d = analyze(p, want(3, 1));
    CHECK(d.constants[9] == 2 && d.initial_temps[2] == 8 && !d.initial_outputs[0]);
    d = analyze(p, want(0, 2));
    CHECK(d.initial_outputs[0] == 2 && !d.constants[9]);
    auto empty = analyze(p, want(0, 0));
    CHECK((empty.inputs == std::array<uint8_t,16>{}));
    CHECK((empty.constants == std::array<uint8_t,192>{}));
}

void rejection_and_final_boundaries() {
    std::array<Nv2aVshStep, 2> p{};
    p[0].mac = op(NV2AOP_MOV, output(NV2ART_OUTPUT, 0, 8),
                  {input(NV2ART_INPUT, 0)});
    p[0].is_final = true;
    p[1].mac = op(NV2AOP_ARL, output(NV2ART_ADDRESS, 0, 0),
                  {input(NV2ART_CONTEXT, 191)});
    CHECK(analyze(p, want(0, 1)).steps == 1); // trailing unexecuted slot ignored
    const auto good = p[0];
    auto reject = [&](const Nv2aVshStep* steps, size_t size,
                      const std::array<uint8_t, 13>& observable) {
        xlle::VshDependencies d;
        d.constants[191] = 15; d.inputs[15] = 7; d.steps = 99;
        const auto before = d;
        const char* error = nullptr;
        CHECK(!xlle::vsh_dependencies(steps, size, observable, d, error));
        CHECK(error && d.constants == before.constants && d.inputs == before.inputs &&
              d.initial_temps == before.initial_temps && d.initial_outputs == before.initial_outputs &&
              d.steps == before.steps);
    };
    reject(nullptr, 1, want(0, 1));
    reject(&good, 0, want(0, 1));
    reject(&good, 137, want(0, 1));
    reject(&good, 1, want(0, 16));
    reject(&good, 1, want(5, 2));
    auto bad = good; bad.is_final = false; reject(&bad, 1, want(0, 1));
    for (auto type : {NV2ART_CONTEXT, NV2ART_ADDRESS, NV2ART_INPUT}) {
        bad = good; bad.mac.outputs[0].type = type;
        reject(&bad, 1, want(3, 1)); // reject even though destination is dead
    }
    bad = good; bad.mac.inputs[0] = input(NV2ART_CONTEXT, 0);
    bad.mac.inputs[0].is_relative = true; reject(&bad, 1, want(0, 1));
    bad = good; bad.mac.inputs[0].type = NV2ART_OUTPUT; reject(&bad, 1, want(0, 1));
    for (auto type : {NV2ART_INPUT, NV2ART_CONTEXT, NV2ART_TEMPORARY}) {
        bad = good; bad.mac.inputs[0] = input(type, 192); reject(&bad, 1, want(0, 1));
    }
    bad = good; bad.mac.inputs[0].swizzle[3] = 4; reject(&bad, 1, want(0, 1));
    bad = good; bad.mac.outputs[0] = output(NV2ART_TEMPORARY, 12, 8); reject(&bad, 1, want(0, 1));
    bad = good; bad.mac.outputs[0] = output(NV2ART_OUTPUT, 13, 8); reject(&bad, 1, want(0, 1));
    bad = good; bad.mac.outputs[0].writemask = Nv2aVshWritemask(16); reject(&bad, 1, want(0, 1));
    bad = good; bad.mac.opcode = NV2AOP_MAD; reject(&bad, 1, want(0, 1));
    bad = good; bad.mac.inputs[1] = input(NV2ART_INPUT, 1); reject(&bad, 1, want(0, 1));
    bad = good; bad.mac.opcode = NV2AOP_DP3; reject(&bad, 1, want(0, 1));
    bad = good; bad.mac.opcode = NV2AOP_RCP; reject(&bad, 1, want(0, 1));
    bad = good; bad.ilu = op(NV2AOP_ADD, output(NV2ART_OUTPUT, 0, 8),
                           {input(NV2ART_INPUT, 0),input(NV2ART_INPUT, 1)});
    reject(&bad, 1, want(0, 1));
    bad = good; bad.ilu = op(NV2AOP_MOV, output(NV2ART_TEMPORARY, 2, 8),
                           {input(NV2ART_INPUT, 0)});
    reject(&bad, 1, want(0, 1));

    std::array<Nv2aVshStep, 136> full{};
    full.back().is_final = true;
    const auto all_nop = analyze(full, want(0, 1));
    CHECK(all_nop.steps == 136 && all_nop.initial_outputs[0] == 1);
    full[0].mac = op(NV2AOP_MAD, output(NV2ART_OUTPUT, 0, 1),
                     {input(NV2ART_INPUT, 15, {2,2,2,2}),
                      input(NV2ART_CONTEXT, 191, {1,1,1,1}),
                      input(NV2ART_CONTEXT, 5, {0,0,0,0})});
    const auto last_registers = analyze(full, want(0, 8));
    CHECK(last_registers.inputs[15] == 4 && last_registers.constants[191] == 2 &&
          last_registers.constants[5] == 1 && !last_registers.initial_outputs[0]);
}
} // namespace

int main() {
    dead_fog_and_scalar_mask();
    partial_stores_swizzles_and_two_destinations();
    parallel_reads_and_last_store_priority();
    r12_alias_and_initial_lanes();
    rejection_and_final_boundaries();
    if (!failures) std::puts("VSH dependency authored controls passed (5 groups)");
    return failures ? 1 : 0;
}
