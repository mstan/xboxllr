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

int main() try {
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
    const auto source_view = probe::kComputeSource;
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
    std::vector<probe::DotInput> inputs;
    for (const auto& item : cases) inputs.push_back(item.input);
    const auto expected = probe::cpu_oracle(cases);
    const size_t dispatch_groups = (inputs.size() + 63) / 64;
    std::vector<probe::DotOutput> actual((dispatch_groups + 1) * 64);
    for (auto& item : actual) item.words.fill(0xdeadbeefu);
    GLuint buffers[2]{};
    GenBuffers(2, buffers);
    BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
    BufferData(GL_SHADER_STORAGE_BUFFER, inputs.size() * sizeof(inputs[0]), inputs.data(), GL_STATIC_DRAW);
    BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffers[0]);
    BindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
    BufferData(GL_SHADER_STORAGE_BUFFER, actual.size() * sizeof(actual[0]), actual.data(), GL_DYNAMIC_READ);
    BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buffers[1]);
    UseProgram(program);
    const GLint count_location = GetUniformLocation(program, "u_record_count");
    if (count_location < 0) throw std::runtime_error("Missing record_count uniform");
    Uniform1ui(count_location, static_cast<GLuint>(inputs.size()));
    DispatchCompute(static_cast<GLuint>(dispatch_groups), 1, 1);
    MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    Finish();
    GetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, actual.size() * sizeof(actual[0]), actual.data());
    if (GetError() != GL_NO_ERROR) throw std::runtime_error("GL dispatch/readback error");
    for (size_t i = inputs.size(); i < actual.size(); ++i)
        for (uint32_t value : actual[i].words)
            if (value != 0xdeadbeefu) throw std::runtime_error("Output guard overwritten");

    std::map<std::string, std::array<size_t, 5>> totals;
    size_t mismatches = 0;
    for (size_t i = 0; i < inputs.size(); ++i) {
        const char* category = cases[i].category.c_str();
        auto& counts = totals[category];
        ++counts[0];
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
                  << " dp4=" << counts[2] << " screen=" << counts[3] << " w=" << counts[4] << '\n';
    std::cout << "total_records=" << inputs.size() << " mismatched_lanes=" << mismatches << " guard=ok\n";
    DeleteBuffers(2, buffers);
    DeleteProgram(program);
    DeleteShader(shader);
    return mismatches ? 3 : 0;
} catch (const std::exception& error) {
    std::cerr << "probe failed: " << error.what() << '\n';
    return 2;
}
