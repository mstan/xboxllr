#include "machine/nv2a_vsh_dependencies.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

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

void ordinary_mac_component_dependencies() {
    // Authored expectations use dependency bits X=1,Y=2,Z=4,W=8.
    // Each source has a different permutation, so vector-wide requirements,
    // unswizzled lanes and accidental operand interchange are distinguishable.
    const std::array<uint8_t,4> a{3,1,0,2}, b{2,0,3,1};
    const uint8_t lane_a[4] = {8,2,1,4}, lane_b[4] = {4,1,8,2};
    for (auto code : {NV2AOP_MIN, NV2AOP_MAX, NV2AOP_SLT, NV2AOP_SGE}) {
        std::array<Nv2aVshStep,1> p{};
        p[0].mac = op(code, output(NV2ART_OUTPUT,0,15),
                      {input(NV2ART_INPUT,2,a),input(NV2ART_CONTEXT,37,b)});
        p[0].mac.inputs[0].is_negated = true;
        p[0].is_final = true;
        for (unsigned lane=0;lane<4;++lane) {
            const auto d=analyze(p,want(0,uint8_t(1u<<lane)));
            CHECK(d.inputs[2]==lane_a[lane] && d.constants[37]==lane_b[lane]);
            CHECK(!d.initial_outputs[0]);
        }
    }
    struct DotCase {Nv2aVshOpcode code;uint8_t a,b;};
    // DPH's implicit A.w=1 must neither require A.w nor discard B.w.
    const DotCase dots[]={{NV2AOP_DP3,11,13},{NV2AOP_DPH,11,15},{NV2AOP_DP4,15,15}};
    for (const auto& test:dots) {
        std::array<Nv2aVshStep,1> p{};
        p[0].mac=op(test.code,output(NV2ART_OUTPUT,0,15),
                    {input(NV2ART_INPUT,2,a),input(NV2ART_CONTEXT,37,b)});
        p[0].is_final=true;
        for (unsigned lane=0;lane<4;++lane) {
            const auto d=analyze(p,want(0,uint8_t(1u<<lane)));
            CHECK(d.inputs[2]==test.a && d.constants[37]==test.b);
        }
        const auto dead=analyze(p,want(0,0));
        CHECK(!dead.inputs[2] && !dead.constants[37]);
    }
    std::array<Nv2aVshStep,1> p{};
    p[0].mac=op(NV2AOP_DST,output(NV2ART_OUTPUT,0,15),
                {input(NV2ART_INPUT,2,a),input(NV2ART_CONTEXT,37,b)});
    p[0].is_final=true;
    // DST=(1,A.y*B.y,A.z,B.w), before source swizzles are mapped back.
    const uint8_t dst_a[4]={0,2,1,0},dst_b[4]={0,1,0,2};
    for (unsigned lane=0;lane<4;++lane) {
        const auto d=analyze(p,want(0,uint8_t(1u<<lane)));
        CHECK(d.inputs[2]==dst_a[lane] && d.constants[37]==dst_b[lane]);
        CHECK(!d.initial_outputs[0]); // constant X is still a real write
    }
    p[0].mac.outputs[0]=output(NV2ART_OUTPUT,5,2); // fog from result Z
    const auto fog=analyze(p,want(5,1));
    CHECK(fog.inputs[2]==1 && !fog.constants[37]);
}

void ordinary_ilu_component_dependencies() {
    const std::array<uint8_t,4> swizzle{3,1,0,2};
    for (auto code : {NV2AOP_RCP,NV2AOP_RCC,NV2AOP_RSQ,NV2AOP_EXP,NV2AOP_LOG,NV2AOP_LIT}) {
        std::array<Nv2aVshStep,1> p{};
        p[0].ilu=op(code,output(NV2ART_OUTPUT,4,15),
                    {input(NV2ART_CONTEXT,73,swizzle)});
        p[0].ilu.inputs[0].is_negated=true;
        p[0].is_final=true;
        for (unsigned lane=0;lane<4;++lane) {
            const auto d=analyze(p,want(4,uint8_t(1u<<lane)));
            uint8_t expected=8; // reciprocal/sqrt or EXP/LOG xyz use operand X
            if ((code==NV2AOP_EXP || code==NV2AOP_LOG) && lane==3)expected=0;
            if (code==NV2AOP_LIT) {
                // Constant X/W, Y depends on X, Z on predicate X and Y/W.
                const uint8_t lit[4]={0,8,14,0};expected=lit[lane];
            }
            CHECK(d.constants[73]==expected && !d.initial_outputs[4]);
        }
        const auto dead=analyze(p,want(4,0));
        CHECK(!dead.constants[73]);
    }
    // A dual destination can need a nonconstant result as well as constant W.
    std::array<Nv2aVshStep,2> p{};
    p[0].ilu=op(NV2AOP_EXP,output(NV2ART_TEMPORARY,1,8),
                {input(NV2ART_INPUT,9,swizzle)});
    p[0].ilu.outputs[1]=output(NV2ART_OUTPUT,4,1);
    p[1].mac=op(NV2AOP_MOV,output(NV2ART_OUTPUT,0,8),
                {input(NV2ART_TEMPORARY,1)});
    p[1].is_final=true;
    auto observable=want(4,8);observable[0]=1;
    const auto both=analyze(p,observable);
    CHECK(both.inputs[9]==8 && !both.initial_temps[1]);
    const auto constant_only=analyze(p,want(4,8));
    CHECK(!constant_only.inputs[9] && !constant_only.initial_outputs[4]);
}

void ordinary_opcode_parallel_hazards() {
    std::array<Nv2aVshStep,2> p{};
    p[0].mac=op(NV2AOP_MOV,output(NV2ART_TEMPORARY,2,15),
                {input(NV2ART_INPUT,3)});
    p[1].mac=op(NV2AOP_DP3,output(NV2ART_TEMPORARY,2,14),
                {input(NV2ART_CONTEXT,4),input(NV2ART_CONTEXT,5)});
    p[1].ilu=op(NV2AOP_RSQ,output(NV2ART_OUTPUT,0,1),
                {input(NV2ART_TEMPORARY,2,{2,0,1,3})});
    p[1].is_final=true;
    const auto old_z=analyze(p,want(0,8));
    CHECK(old_z.inputs[3]==4 && !old_z.constants[4] && !old_z.constants[5]);
    CHECK(!old_z.initial_temps[2]); // same-step MAC cannot define ILU's input

    p={};
    p[0].mac=op(NV2AOP_DP4,output(NV2ART_TEMPORARY,1,1),
                {input(NV2ART_INPUT,6),input(NV2ART_CONTEXT,7)});
    p[0].ilu=op(NV2AOP_EXP,output(NV2ART_TEMPORARY,1,1),
                {input(NV2ART_INPUT,8,{2,1,3,0})});
    p[1].mac=op(NV2AOP_MOV,output(NV2ART_OUTPUT,0,1),
                {input(NV2ART_TEMPORARY,1)});
    p[1].is_final=true;
    auto d=analyze(p,want(0,8));
    CHECK(!d.inputs[6] && !d.constants[7] && !d.inputs[8] && !d.initial_temps[1]);
    // The ILU's constant result W kills the MAC dot result, not its sources.
    p[0].ilu.opcode=NV2AOP_RCC;
    d=analyze(p,want(0,8));
    CHECK(!d.inputs[6] && !d.constants[7] && d.inputs[8]==4 && !d.initial_temps[1]);
}

struct VertexSources {
    std::array<std::array<float,4>,16> inputs{};
    std::array<uint8_t,16> input_known{};
    std::array<std::array<float,4>,192> constants{};
    std::array<uint8_t,192> constant_known{};
};

template<size_t N>
bool resolve_vertex(const std::array<Nv2aVshStep,N>& p, const VertexSources& source,
                    const std::array<uint8_t,13>& observable,
                    xlle::VshDependencies& d, const char*& error) {
    std::array<unsigned char,sizeof(p)> before{};
    std::memcpy(before.data(),p.data(),sizeof(p));
    const bool ok=xlle::vsh_dependencies_for_vertex(p.data(),p.size(),observable,
        source.inputs,source.input_known,source.constants,source.constant_known,d,error);
    CHECK(std::memcmp(before.data(),p.data(),sizeof(p))==0);
    return ok;
}

template<size_t N>
xlle::VshDependencies analyze_vertex(const std::array<Nv2aVshStep,N>& p,
                                    const VertexSources& source,
                                    const std::array<uint8_t,13>& observable) {
    xlle::VshDependencies d;
    const char* error=nullptr;
    CHECK(resolve_vertex(p,source,observable,d,error));
    CHECK(!error);
    return d;
}

template<size_t N>
void reject_vertex(const std::array<Nv2aVshStep,N>& p,const VertexSources& source,
                   const std::array<uint8_t,13>& observable=want(0,1)) {
    xlle::VshDependencies d;
    d.inputs.fill(3);d.constants.fill(5);d.initial_temps.fill(7);
    d.initial_outputs.fill(9);d.steps=99;
    const auto before=d;
    const char* error=nullptr;
    CHECK(!resolve_vertex(p,source,observable,d,error));
    CHECK(error && d.inputs==before.inputs && d.constants==before.constants &&
          d.initial_temps==before.initial_temps && d.initial_outputs==before.initial_outputs &&
          d.steps==before.steps);
}

Nv2aVshInput relative(unsigned base,std::array<uint8_t,4> swizzle={0,1,2,3}) {
    auto in=input(NV2ART_CONTEXT,base,swizzle);in.is_relative=true;return in;
}

void indexed_input_bias_and_masks() {
    std::array<Nv2aVshStep,2> p{};
    p[0].mac=op(NV2AOP_ARL,output(NV2ART_ADDRESS,0,0),
                {input(NV2ART_INPUT,2,{3,0,1,2})});
    p[1].mac=op(NV2AOP_MOV,output(NV2ART_OUTPUT,0,8),
                {relative(5,{1,0,3,2})});
    p[1].is_final=true;
    VertexSources source;
    source.input_known[2]=8; // only the actual ARL source W has a producer
    struct Case {float value;bool negated;unsigned selected;};
    // Explicit expectations include the maintained ARL +0.001f bias.
    // Plain floor, truncation, ignored negation and ignored swizzle differ.
    const Case cases[]={{0.9995f,false,6},{0.9985f,false,5},
        {-0.0005f,false,5},{-0.0015f,false,4},{-1.25f,false,3},
        {1.25f,true,3},{-1.25f,true,6}};
    for(const auto& test:cases) {
        source.inputs[2][3]=test.value;
        p[0].mac.inputs[0].is_negated=test.negated;
        const auto d=analyze_vertex(p,source,want(0,1));
        auto expected_inputs=std::array<uint8_t,16>{};expected_inputs[2]=8;
        auto expected_constants=std::array<uint8_t,192>{};expected_constants[test.selected]=2;
        CHECK(d.inputs==expected_inputs && d.constants==expected_constants && d.steps==2);
        CHECK(!d.initial_outputs[0]);
    }
    // Ordinary output dependencies stay caller-checked, rather than demanding
    // all their producer-known bits inside this address-only resolver.
    p[1].mac.inputs[0]=input(NV2ART_INPUT,3,{2,0,1,3});
    const auto ordinary=analyze_vertex(p,source,want(0,1));
    CHECK(ordinary.inputs[2]==8 && ordinary.inputs[3]==4 && !source.input_known[3]);
    // Executed ARL source lanes remain required even when no output is live.
    const auto dead=analyze_vertex(p,source,want(0,0));
    CHECK(dead.inputs[2]==8 && !dead.inputs[3]);
    source.input_known[2]=7;reject_vertex(p,source); // wrong known lanes
    source.input_known[2]=0;reject_vertex(p,source);
}

void indexed_constant_and_parallel_address() {
    std::array<Nv2aVshStep,3> p{};
    p[0].mac=op(NV2AOP_ARL,output(NV2ART_ADDRESS,0,0),{input(NV2ART_INPUT,0)});
    p[1].mac=op(NV2AOP_ARL,output(NV2ART_ADDRESS,0,0),
                {input(NV2ART_CONTEXT,10,{2,0,1,3})});
    p[1].ilu=op(NV2AOP_MOV,output(NV2ART_OUTPUT,4,1),{relative(20,{0,1,3,2})});
    p[2].mac=op(NV2AOP_MOV,output(NV2ART_OUTPUT,0,8),{relative(20,{1,0,2,3})});
    p[2].is_final=true;
    VertexSources source;
    source.inputs[0][0]=1.0f;source.input_known[0]=1;
    source.constants[10][2]=3.9995f;source.constant_known[10]=4;
    auto observable=want(0,1);observable[4]=8;
    const auto d=analyze_vertex(p,source,observable);
    // ILU consumes OLD a0=1, then the following step sees ARL's biased a0=4.
    auto expected_inputs=std::array<uint8_t,16>{};expected_inputs[0]=1;
    CHECK(d.inputs==expected_inputs);
    auto expected=std::array<uint8_t,192>{};expected[10]=4;expected[21]=4;expected[24]=2;
    CHECK(d.constants==expected && d.steps==3);
    const auto dead=analyze_vertex(p,source,want(0,0));
    CHECK(dead.inputs[0]==1 && dead.constants[10]==4 && !dead.constants[21] && !dead.constants[24]);
    source.constant_known[10]=11;reject_vertex(p,source,observable);
    source.constant_known[10]=4;
    // Replacing ARL with NOP must not erase original paired-ILU R1 rules.
    p[1].ilu.outputs[0]=output(NV2ART_TEMPORARY,2,1);
    reject_vertex(p,source,observable);
}

void indexed_bounds_and_nonfinite() {
    std::array<Nv2aVshStep,2> p{};
    p[0].mac=op(NV2AOP_ARL,output(NV2ART_ADDRESS,0,0),{input(NV2ART_INPUT,0)});
    p[1].mac=op(NV2AOP_MOV,output(NV2ART_OUTPUT,0,8),{relative(0)});
    p[1].is_final=true;
    VertexSources source;source.input_known[0]=1;
    CHECK(analyze_vertex(p,source,want(0,1)).constants[0]==1);
    source.inputs[0][0]=191.0f;
    CHECK(analyze_vertex(p,source,want(0,1)).constants[191]==1);
    source.inputs[0][0]=192.0f;reject_vertex(p,source,want(0,0));
    source.inputs[0][0]=-1.0f;reject_vertex(p,source,want(0,0)); // uint32 wrap
    // Even a constant scalar result fetches its entire decoded operand first.
    p[1].mac={};p[1].ilu=op(NV2AOP_EXP,output(NV2ART_OUTPUT,0,1),{relative(0)});
    reject_vertex(p,source,want(0,8));
    p[1].ilu.inputs[0].index=1; // base1 + signed(-1) wraps to valid c0
    CHECK(!analyze_vertex(p,source,want(0,8)).constants[0]);

    std::array<Nv2aVshStep,1> arl{};arl[0]=p[0];arl[0].is_final=true;
    // Largest/smallest in-range floats must be checked without float(INT_MAX),
    // which rounds to 2147483648 and would allow an undefined integer cast.
    for(float value:{-2147483648.0f,2147483520.0f}) {
        source.inputs[0][0]=value;
        CHECK(analyze_vertex(arl,source,want(0,0)).inputs[0]==1);
    }
    for(float value:{2147483648.0f,-2147483904.0f,
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::quiet_NaN()}) {
        source.inputs[0][0]=value;reject_vertex(arl,source,want(0,0));
    }
}

void indexed_structural_rejections() {
    std::array<Nv2aVshStep,2> good{};
    good[0].mac=op(NV2AOP_ARL,output(NV2ART_ADDRESS,0,0),{input(NV2ART_INPUT,0)});
    good[1].mac=op(NV2AOP_MOV,output(NV2ART_OUTPUT,0,8),{relative(0)});
    good[1].is_final=true;
    VertexSources source;source.input_known.fill(15);source.constant_known.fill(15);
    auto bad=good;
    bad[0].mac=good[1].mac;bad[1].mac=good[0].mac;
    reject_vertex(bad,source,want(0,0)); // relative read before any ARL
    bad=good;bad[0].ilu=op(NV2AOP_MOV,output(NV2ART_OUTPUT,4,8),{relative(0)});
    reject_vertex(bad,source,want(0,0)); // first ARL cannot supply same-step ILU
    for(auto type:{NV2ART_TEMPORARY,NV2ART_OUTPUT,NV2ART_ADDRESS}) {
        bad=good;bad[0].mac.inputs[0]=input(type,0);reject_vertex(bad,source);
    }
    bad=good;bad[1].mac=good[0].mac;
    bad[1].mac.inputs[0]=relative(0);reject_vertex(bad,source); // computed relative ARL source
    for(auto dst:{output(NV2ART_ADDRESS,1,0),output(NV2ART_ADDRESS,0,8),
                  output(NV2ART_TEMPORARY,0,8),output(NV2ART_CONTEXT,0,8),
                  output(NV2ART_OUTPUT,0,8),output(NV2ART_NONE,0,0)}) {
        bad=good;bad[0].mac.outputs[0]=dst;reject_vertex(bad,source);
    }
    bad=good;bad[0].mac.outputs[1]=output(NV2ART_CONTEXT,0,8);reject_vertex(bad,source);
    bad=good;bad[0].mac.inputs[1]=input(NV2ART_INPUT,1);reject_vertex(bad,source);
    bad=good;bad[0].mac.inputs[0].swizzle[3]=4;reject_vertex(bad,source);
    bad=good;bad[1].mac.outputs[0]=output(NV2ART_CONTEXT,3,8);reject_vertex(bad,source);
    bad=good;bad[1].mac.opcode=Nv2aVshOpcode(255);reject_vertex(bad,source,want(0,0));
    // Final prefix ownership and source immutability also apply to indexed mode.
    bad=good;bad[0].is_final=true;bad[1].mac.opcode=Nv2aVshOpcode(255);
    CHECK(analyze_vertex(bad,source,want(0,0)).steps==1);
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
    for (auto code : {NV2AOP_RCC,NV2AOP_RSQ,NV2AOP_EXP,NV2AOP_LOG,NV2AOP_LIT,NV2AOP_ARL}) {
        bad=good;bad.mac.opcode=code;reject(&bad,1,want(0,1));
    }
    bad=good;bad.mac.opcode=Nv2aVshOpcode(255);reject(&bad,1,want(0,1));
    // A constant observable result must not waive validation of executed
    // relative/state writes, invalid swizzles or malformed operand arity.
    bad=good;bad.mac=op(NV2AOP_DST,output(NV2ART_OUTPUT,0,8),
                       {input(NV2ART_INPUT,1),input(NV2ART_CONTEXT,3)});
    bad.mac.inputs[1].is_relative=true;reject(&bad,1,want(0,1));
    bad.mac.inputs[1].is_relative=false;bad.mac.inputs[1].swizzle[3]=4;reject(&bad,1,want(0,1));
    bad.mac.inputs[1].swizzle[3]=3;bad.mac.inputs[2]=input(NV2ART_INPUT,4);reject(&bad,1,want(0,1));
    bad=good;bad.ilu=op(NV2AOP_LOG,output(NV2ART_CONTEXT,4,1),
                       {input(NV2ART_INPUT,0)});reject(&bad,1,want(0,1));
    for (auto code : {NV2AOP_DP3,NV2AOP_DPH,NV2AOP_DP4,NV2AOP_DST,NV2AOP_MIN,NV2AOP_MAX,NV2AOP_SLT,NV2AOP_SGE}) {
        bad=good;bad.ilu=op(code,output(NV2ART_OUTPUT,0,8),
                           {input(NV2ART_INPUT,0),input(NV2ART_CONTEXT,1)});
        reject(&bad,1,want(0,1));
    }
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
    ordinary_mac_component_dependencies();
    ordinary_ilu_component_dependencies();
    ordinary_opcode_parallel_hazards();
    indexed_input_bias_and_masks();
    indexed_constant_and_parallel_address();
    indexed_bounds_and_nonfinite();
    indexed_structural_rejections();
    rejection_and_final_boundaries();
    if (!failures) std::puts("VSH dependency authored controls passed (12 groups)");
    return failures ? 1 : 0;
}
