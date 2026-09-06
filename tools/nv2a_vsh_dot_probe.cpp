#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "nv2a_vsh_dot_probe_cases.hpp"
#include "host/nv2a_vsh_dot_probe_glsl.hpp"
#include "host/nv2a_vsh_dot_probe_fp64_glsl.hpp"
#include "host/nv2a_vsh_program_probe_glsl.hpp"
#include "nv2a_vsh_capture_oracle.hpp"

namespace probe = xlle::vsh_shadow_probe;

template<class T> T load(const char* name) {
    auto pointer = SDL_GL_GetProcAddress(name);
    if (!pointer) throw std::runtime_error(std::string("Missing GL function ") + name);
    return reinterpret_cast<T>(pointer);
}

struct Context {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    ~Context() {
        if (context) SDL_GL_DestroyContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

int main(int argc, char** argv) try {
    bool fp64 = false;
    bool program_self_test = false;
    const char* capture_path = nullptr;
    if (argc == 2 && std::string_view(argv[1]) == "--fp64-rounding") fp64 = true;
    else if (argc == 2 && std::string_view(argv[1]) == "--program-self-test") program_self_test = true;
    else if (argc == 3 && std::string_view(argv[1]) == "--program-capture") capture_path = argv[2];
    else if (argc != 1) throw std::runtime_error("Usage: xlle_vsh_dot_probe [--fp64-rounding | --program-self-test | --program-capture path]");
    const bool program_mode = capture_path || program_self_test;
    Context context;
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE))
        throw std::runtime_error(SDL_GetError());
    context.window = SDL_CreateWindow("VSH numeric probe", 64, 64,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!context.window) throw std::runtime_error(SDL_GetError());
    context.context = SDL_GL_CreateContext(context.window);
    if (!context.context) throw std::runtime_error(SDL_GetError());
#define LOAD(type, name) const auto name = load<type>("gl" #name)
    LOAD(PFNGLCREATESHADERPROC, CreateShader);
    LOAD(PFNGLSHADERSOURCEPROC, ShaderSource);
    LOAD(PFNGLCOMPILESHADERPROC, CompileShader);
    LOAD(PFNGLGETSHADERIVPROC, GetShaderiv);
    LOAD(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog);
    LOAD(PFNGLCREATEPROGRAMPROC, CreateProgram);
    LOAD(PFNGLATTACHSHADERPROC, AttachShader);
    LOAD(PFNGLLINKPROGRAMPROC, LinkProgram);
    LOAD(PFNGLGETPROGRAMIVPROC, GetProgramiv);
    LOAD(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog);
    LOAD(PFNGLUSEPROGRAMPROC, UseProgram);
    LOAD(PFNGLDELETEPROGRAMPROC, DeleteProgram);
    LOAD(PFNGLDELETESHADERPROC, DeleteShader);
    LOAD(PFNGLGENBUFFERSPROC, GenBuffers);
    LOAD(PFNGLBINDBUFFERPROC, BindBuffer);
    LOAD(PFNGLBUFFERDATAPROC, BufferData);
    LOAD(PFNGLBINDBUFFERBASEPROC, BindBufferBase);
    LOAD(PFNGLGETBUFFERSUBDATAPROC, GetBufferSubData);
    LOAD(PFNGLDELETEBUFFERSPROC, DeleteBuffers);
    LOAD(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation);
    LOAD(PFNGLUNIFORM1UIPROC, Uniform1ui);
    LOAD(PFNGLDISPATCHCOMPUTEPROC, DispatchCompute);
    LOAD(PFNGLMEMORYBARRIERPROC, MemoryBarrier);
#undef LOAD
    using GetStringFn = const GLubyte* (APIENTRY*)(GLenum);
    using GetErrorFn = GLenum (APIENTRY*)();
    using FinishFn = void (APIENTRY*)();
    const auto GetString = load<GetStringFn>("glGetString");
    const auto GetError = load<GetErrorFn>("glGetError");
    const auto Finish = load<FinishFn>("glFinish");
    std::cout << "GL=" << GetString(GL_VERSION) << " renderer=" << GetString(GL_RENDERER) << '\n';
    std::cout << "arithmetic=" << (program_mode ? "program-fp64-rounding" : fp64 ? "fp64-rounding" : "native-float") << '\n';
    const auto source_view = program_mode ? std::string_view(probe::kProgramComputeSource) :
        fp64 ? std::string_view(probe::kFp64ComputeSource) : probe::kComputeSource;
    const char* source = source_view.data();
    const GLint length = static_cast<GLint>(source_view.size());
    const GLuint shader = CreateShader(GL_COMPUTE_SHADER);
    ShaderSource(shader, 1, &source, &length);
    CompileShader(shader);
    GLint ok = 0;
    GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    std::array<char, 8192> log{};
    if (!ok) {
        GetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        throw std::runtime_error(log.data());
    }
    const GLuint program = CreateProgram();
    AttachShader(program, shader);
    LinkProgram(program);
    GetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        throw std::runtime_error(log.data());
    }

    if (program_mode) {
        namespace capture = xlle::vsh_capture;
        auto vertices = program_self_test ? xlle::vsh_capture_oracle::make_program_probe_cases() : capture::read(capture_path);
        if (program_self_test) {
            // Deliberate reciprocal/sqrt boundaries and wide finite inputs.
            const std::array<uint32_t, 20> edges{0u, 0x80000000u, 1u, 0x80000001u,
                0x007fffffu, 0x00800000u, 0x3f7fffffu, 0x3f800000u, 0x3f800001u,
                0x3fffffffu, 0x40000000u, 0x40800000u, 0x7f7fffffu, 0x7f800000u,
                0xff800000u, 0x7fc00001u, 0x1f7ffffdu, 0x1f800000u, 0x5f800000u, 0x5f82c2dcu};
            uint32_t random = 0x765321abu;
            for (size_t i = 0; i < 10000 + edges.size(); ++i) {
                capture::Vertex vertex;
                vertex.ordinal = vertices.size() + 1;
                vertex.fingerprint = 1; // synthetic category, no dispatch key
                vertex.inputs[0] = i < edges.size() ? edges[i] : probe::make_random_normal_finite_bits(random);
                for (uint32_t code : {14u, 15u, 16u}) {
                    const uint32_t dest = code == 14u ? 0u : code == 15u ? 3u : 4u;
                    vertex.ops.push_back(xlle::vsh_capture_oracle::detail::make_wire_op(
                        code, 1, xlle::kVshBaseOutput, dest, 15));
                }
                vertex.expected = xlle::vsh_capture_oracle::validate_and_execute(vertex);
                vertices.push_back(std::move(vertex));
            }
            for (auto& vertex : vertices) {
                std::copy_n(vertex.expected.begin(), 4, vertex.post_position.begin());
                for (size_t lane : {size_t(0), size_t(1)})
                    vertex.post_position[lane] = probe::float_to_bits(probe::round_screen_coordinate(
                        probe::bits_to_float(vertex.post_position[lane])));
                vertex.post_position[3] = probe::float_to_bits(probe::clamp_away_zero_inf(
                    probe::bits_to_float(vertex.post_position[3])));
            }
        }
        // 16 input +192 context uvec4s, followed by op offset/count/reserved.
        std::vector<std::array<uint32_t, 836>> inputs(vertices.size());
        std::vector<capture::Op> ops;
        std::vector<bool> expected_numeric_decline(vertices.size());
        std::map<uint64_t, std::array<size_t, 5>> counts;
        for (size_t i = 0; i < vertices.size(); ++i) {
            const auto& vertex = vertices[i];
            const auto cpu = xlle::vsh_capture_oracle::validate_and_execute(vertex);
            const auto trace = xlle::vsh_capture_oracle::trace_numeric_decline(vertex);
            if (trace.outputs != cpu) throw std::runtime_error("CPU numeric trace differs from compiled executor");
            expected_numeric_decline[i] = trace.numeric_decline;
            if (cpu != vertex.expected)
                throw std::runtime_error("Captured CPU output differs from independent full-zero execution, ordinal " +
                                         std::to_string(vertex.ordinal));
            std::array<uint32_t, 4> post{cpu[0], cpu[1], cpu[2], cpu[3]};
            post[0] = probe::float_to_bits(probe::round_screen_coordinate(probe::bits_to_float(post[0])));
            post[1] = probe::float_to_bits(probe::round_screen_coordinate(probe::bits_to_float(post[1])));
            post[3] = probe::float_to_bits(probe::clamp_away_zero_inf(probe::bits_to_float(post[3])));
            if (post != vertex.post_position)
                throw std::runtime_error("Captured position postprocessing differs from CPU oracle");
            auto& words = inputs[i];
            std::copy(vertex.inputs.begin(), vertex.inputs.end(), words.begin());
            std::copy(vertex.constants.begin(), vertex.constants.end(), words.begin() + 64);
            words[832] = static_cast<uint32_t>(ops.size());
            words[833] = static_cast<uint32_t>(vertex.ops.size());
            ops.insert(ops.end(), vertex.ops.begin(), vertex.ops.end());
            ++counts[vertex.fingerprint][0];
        }
        const size_t groups = (vertices.size() + 63) / 64;
        std::vector<std::array<uint32_t, 32>> actual((groups + 1) * 64);
        std::vector<uint32_t> status(actual.size(), 0xdeadbeefu);
        for (auto& item : actual) item.fill(0xdeadbeefu);
        GLuint buffers[4]{};
        GenBuffers(4, buffers);
        const auto upload = [&](unsigned slot, const void* data, size_t bytes) {
            BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[slot]);
            BufferData(GL_SHADER_STORAGE_BUFFER, bytes, data, GL_DYNAMIC_READ);
            BindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, buffers[slot]);
        };
        upload(0, inputs.data(), inputs.size() * sizeof(inputs[0]));
        upload(1, ops.data(), ops.size() * sizeof(ops[0]));
        upload(2, actual.data(), actual.size() * sizeof(actual[0]));
        upload(3, status.data(), status.size() * sizeof(status[0]));
        UseProgram(program);
        const GLint location = GetUniformLocation(program, "u_record_count");
        if (location < 0) throw std::runtime_error("Missing program count uniform");
        Uniform1ui(location, static_cast<GLuint>(vertices.size()));
        DispatchCompute(static_cast<GLuint>(groups), 1, 1);
        MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
        Finish();
        BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[2]);
        GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, actual.size() * sizeof(actual[0]), actual.data());
        BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[3]);
        GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, status.size() * sizeof(status[0]), status.data());
        if (GetError() != GL_NO_ERROR) throw std::runtime_error("Program probe GL error");
        size_t mismatches = 0, numeric_declines = 0, program_declines = 0;
        for (size_t i = 0; i < actual.size(); ++i) {
            if (i >= vertices.size()) {
                if (status[i] != 0xdeadbeefu) throw std::runtime_error("Program status guard overwritten");
            } else if (status[i] > 2) throw std::runtime_error("Invalid or missing program status");
            if (i < vertices.size()) {
                const bool unsupported_program = std::any_of(vertices[i].ops.begin(), vertices[i].ops.end(),
                    [](const capture::Op& op) { return op.words[0] == 13u || op.words[0] >= 17u; });
                if ((status[i] == 2u) != unsupported_program)
                    throw std::runtime_error("GPU program admission differs from decoded host plan");
                if (!unsupported_program && (status[i] == 1u) != expected_numeric_decline[i])
                    throw std::runtime_error("GPU numeric decline differs from CPU intermediate trace, ordinal " +
                                             std::to_string(vertices[i].ordinal));
            }
            if (i >= vertices.size() || status[i]) {
                for (uint32_t word : actual[i])
                    if (word != 0xdeadbeefu) throw std::runtime_error("Declined program output or guard written");
                if (i < vertices.size()) {
                    ++counts[vertices[i].fingerprint][status[i]];
                    if (status[i] == 1) {
                        ++numeric_declines;
                        if (counts[vertices[i].fingerprint][1] <= 8) {
                            const auto nan_lanes = std::count_if(vertices[i].expected.begin(), vertices[i].expected.end(),
                                [](uint32_t word) { return (word & 0x7fffffffu) > 0x7f800000u; });
                            std::cout << "numeric_decline ordinal=" << vertices[i].ordinal << " fp=" << std::hex
                                      << vertices[i].fingerprint << std::dec << " cpu_output_nan_lanes=" << nan_lanes << '\n';
                        }
                    } else ++program_declines;
                }
                continue;
            }
            const auto& vertex = vertices[i];
            for (size_t lane = 0; lane < 32; ++lane) {
                const uint32_t expected = lane < 28 ? vertex.expected[lane] : vertex.post_position[lane - 28];
                if (actual[i][lane] == expected) continue;
                ++mismatches;
                auto& category_count = counts[vertex.fingerprint][lane < 28 ? 3 : 4];
                if (++category_count == 1)
                    std::cout << "mismatch ordinal=" << vertex.ordinal << " fp=" << std::hex << vertex.fingerprint
                              << std::dec << " lane=" << lane << " cpu=0x" << std::hex << expected
                              << " gpu=0x" << actual[i][lane] << std::dec << '\n';
            }
        }
        for (const auto& [fp, count] : counts)
            std::cout << "program fp=" << std::hex << fp << std::dec << " records=" << count[0]
                      << " numeric_declines=" << count[1] << " program_declines=" << count[2]
                      << " raw_mismatches=" << count[3] << " position_mismatches=" << count[4] << '\n';
        std::cout << "program_records=" << vertices.size() << " cpu_revalidated=" << vertices.size()
                  << " numeric_declines=" << numeric_declines << " program_declines=" << program_declines
                  << " mismatched_lanes=" << mismatches << " guard=ok\n";
        DeleteBuffers(4, buffers); DeleteProgram(program); DeleteShader(shader);
        return mismatches ? 3 : (numeric_declines || program_declines) ? 4 : 0;
    }

    static_assert(sizeof(probe::DotInput) == 32);
    static_assert(sizeof(probe::DotOutput) == 16);
    auto cases = probe::make_cases(100000);
    uint32_t random = 0x89abcdef;
    for (size_t i = 0; i < 100003; ++i) {
        probe::DotInput record;
        for (size_t lane = 0; lane < 4; ++lane) {
            const auto next = [&]() {
                random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                return static_cast<float>(static_cast<int32_t>(random % 2000001u) - 1000000) / 1024.0f;
            };
            record.a[lane] = probe::float_to_bits(next());
            record.b[lane] = probe::float_to_bits(next());
        }
        cases.push_back({record, "bounded-finite"});
    }
    probe::append_rounding_cases(cases);
    std::vector<probe::DotInput> inputs;
    for (const auto& item : cases) inputs.push_back(item.input);
    const auto expected = probe::cpu_oracle(cases);
    // Independent directed values catch an unsuitable host rounding/FTZ mode
    // or a corpus change that accidentally stops exercising these boundaries.
    const std::map<std::string, uint32_t> known_dp4 = {
        {"all_negative_zero", 0x80000000u},
        {"normal_input_w_sign", 0x80000122u},
        {"round_tie_add_odd_positive", 0x3f800002u},
        {"product_must_round_before_cancellation", 0u},
        {"product_normal_halfway_odd", 0x3fc00002u},
        {"product_half_min_subnormal_even_zero", 0u},
        {"product_subnormal_halfway_odd", 2u},
        {"product_halfway_at_min_normal", 0x00800000u},
        {"sum_overflow_below_threshold", 0u},
        {"sum_overflow_tie_threshold", 0x7f7fffffu},
    };
    size_t checked_oracle_cases = 0;
    for (size_t i = 0; i < cases.size(); ++i) {
        const auto known = known_dp4.find(cases[i].category);
        if (known == known_dp4.end()) continue;
        if (expected[i].words[1] != known->second)
            throw std::runtime_error("CPU oracle sanity check failed: " + cases[i].category);
        ++checked_oracle_cases;
    }
    if (checked_oracle_cases != known_dp4.size())
        throw std::runtime_error("Missing or duplicated directed oracle cases");
    std::cout << "oracle_sanity_cases=" << checked_oracle_cases << '\n';
    const size_t dispatch_groups = (inputs.size() + 63) / 64;
    std::vector<probe::DotOutput> actual((dispatch_groups + 1) * 64);
    for (auto& item : actual) item.words.fill(0xdeadbeefu);
    std::vector<uint32_t> status(actual.size(), 0xdeadbeefu);
    GLuint buffers[3]{};
    GenBuffers(3, buffers);
    BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
    BufferData(GL_SHADER_STORAGE_BUFFER, inputs.size() * sizeof(inputs[0]), inputs.data(), GL_STATIC_DRAW);
    BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffers[0]);
    BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
    BufferData(GL_SHADER_STORAGE_BUFFER, actual.size() * sizeof(actual[0]), actual.data(), GL_DYNAMIC_READ);
    BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buffers[1]);
    if (fp64) {
        BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[2]);
        BufferData(GL_SHADER_STORAGE_BUFFER, status.size() * sizeof(status[0]), status.data(), GL_DYNAMIC_READ);
        BindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, buffers[2]);
    }
    UseProgram(program);
    const GLint count_location = GetUniformLocation(program, "u_record_count");
    if (count_location < 0) throw std::runtime_error("Missing record_count uniform");
    Uniform1ui(count_location, static_cast<GLuint>(inputs.size()));
    DispatchCompute(static_cast<GLuint>(dispatch_groups), 1, 1);
    MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    Finish();
    BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
    GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, actual.size() * sizeof(actual[0]), actual.data());
    if (fp64) {
        BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[2]);
        GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, status.size() * sizeof(status[0]), status.data());
    }
    if (GetError() != GL_NO_ERROR) throw std::runtime_error("GL dispatch/readback error");
    for (size_t i = inputs.size(); i < actual.size(); ++i)
        for (uint32_t value : actual[i].words)
            if (value != 0xdeadbeefu) throw std::runtime_error("Output guard overwritten");
    if (fp64)
        for (size_t i = inputs.size(); i < status.size(); ++i)
            if (status[i] != 0xdeadbeefu) throw std::runtime_error("Status guard overwritten");

    std::map<std::string, std::array<size_t, 6>> totals;
    size_t mismatches = 0;
    size_t declined = 0;
    for (size_t i = 0; i < inputs.size(); ++i) {
        const char* category = cases[i].category.c_str();
        auto& counts = totals[category];
        ++counts[0];
        if (fp64) {
            if (status[i] > 1u) throw std::runtime_error("Missing or invalid record status");
            const bool expected_nan = (expected[i].words[0] & 0x7fffffffu) > 0x7f800000u ||
                                      (expected[i].words[1] & 0x7fffffffu) > 0x7f800000u;
            if ((status[i] == 1u) != expected_nan)
                throw std::runtime_error("Decline policy differs from CPU NaN result at case " + std::to_string(i));
            if (status[i] == 1u) {
                for (uint32_t value : actual[i].words)
                    if (value != 0xdeadbeefu) throw std::runtime_error("Declined output was written");
                ++counts[5];
                ++declined;
                continue;
            }
        }
        for (size_t lane = 0; lane < 4; ++lane) {
            if (actual[i].words[lane] == expected[i].words[lane]) continue;
            ++counts[lane + 1];
            ++mismatches;
            if (counts[lane + 1] == 1) {
                std::cout << "mismatch case=" << i << " category=" << category << " lane=" << lane
                          << " cpu=0x" << std::hex << expected[i].words[lane]
                          << " gpu=0x" << actual[i].words[lane] << std::dec << '\n';
                if (lane == 3) {
                    std::cout << "witness inputs=" << std::hex;
                    for (uint32_t value : inputs[i].a) std::cout << " 0x" << value;
                    for (uint32_t value : inputs[i].b) std::cout << " 0x" << value;
                    std::cout << std::dec << '\n';
                }
            }
        }
    }
    for (const auto& [category, counts] : totals)
        std::cout << category << " records=" << counts[0] << " mismatches_dp3=" << counts[1]
                  << " dp4=" << counts[2] << " screen=" << counts[3] << " w=" << counts[4]
                  << " declined=" << counts[5] << '\n';
    std::cout << "total_records=" << inputs.size() << " compared_records=" << inputs.size() - declined
              << " declined_records=" << declined << " mismatched_lanes=" << mismatches << " guard=ok\n";
    DeleteBuffers(3, buffers);
    DeleteProgram(program);
    DeleteShader(shader);
    return mismatches ? 3 : declined ? 4 : 0;
} catch (const std::exception& error) {
    std::cerr << "probe failed: " << error.what() << '\n';
    return 2;
}
