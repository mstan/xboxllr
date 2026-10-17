// Portable input-only replay of the maintained loaded-shader admission helper.
// File words are little-endian IEEE binary32; decoding does not depend on host
// byte order. This runs no GPU, title CPU, audio sink or original allocation.
#include "machine/nv2a_vsh_dependencies.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Input {
    std::ifstream file;
    explicit Input(const char* path) : file(path, std::ios::binary) {
        require(bool(file), "Cannot open captured inputs");
    }
    uint32_t word() {
        unsigned char bytes[4];
        require(bool(file.read(reinterpret_cast<char*>(bytes), 4)), "Truncated captured inputs");
        return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 |
            uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
    }
    template<size_t N> std::array<uint8_t,N> masks() {
        std::array<uint8_t,N> value{};
        require(bool(file.read(reinterpret_cast<char*>(value.data()), N)), "Truncated lane masks");
        for (auto mask : value) require(mask <= 15, "Invalid lane mask");
        return value;
    }
    template<size_t N> std::array<std::array<float,4>,N> vectors() {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                      "Captured shader inputs require IEEE binary32");
        std::array<std::array<float,4>,N> value{};
        for (auto& vector : value) for (float& lane : vector) {
            const uint32_t bits = word();
            std::memcpy(&lane, &bits, 4);
        }
        return value;
    }
};
struct Program {
    Nv2aVshProgram value{};
    ~Program() { nv2a_vsh_program_destroy(&value); }
};
using Inputs = std::array<std::array<float,4>,16>;
using Constants = std::array<std::array<float,4>,192>;
bool admit(const Program& program, uint32_t slots,
           const std::array<uint8_t,13>& observable, const Inputs& inputs,
           const std::array<uint8_t,16>& input_known, const Constants& constants,
           const std::array<uint8_t,192>& constant_known, xlle::VshDependencies& dependencies) {
    const char* error = nullptr;
    if (!xlle::vsh_dependencies_for_vertex(program.value.steps, slots, observable,
            inputs, input_known, constants, constant_known, dependencies, error)) return false;
    // The renderer checks returned ordinary dependencies after resolving ARL.
    for (size_t i=0; i<input_known.size(); ++i)
        if (dependencies.inputs[i] & ~input_known[i]) return false;
    for (size_t i=0; i<constant_known.size(); ++i)
        if (dependencies.constants[i] & ~constant_known[i]) return false;
    return true;
}
uint8_t first_lane(uint8_t mask) { return static_cast<uint8_t>(mask & (~unsigned(mask)+1)); }
}

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: native_vsh_dependency_replay captured-inputs.bin\n"; return 2; }
    try {
        Input input(argv[1]);
        require(input.word()==0x50534856, "Invalid admission capture magic");
        const auto version=input.word(), count=input.word();
        require((version==1 || version==2) && count && count<=32768, "Invalid admission version/count");
        uint64_t vertices=0, constant_controls=0, input_controls=0;
        std::set<uint32_t> identities;
        for (uint32_t index=0; index<count; ++index) {
            const auto identity=input.word(), slots=input.word(), size=input.word();
            require(identity && identities.insert(identity).second, "Duplicate/missing draw identity");
            require(slots && slots<=136 && size && size<=65536 && vertices+size<=1000000,
                    "Invalid program/vertex extent");
            if (version==2) require(input.word()==1, "Unknown fixture availability policy");
            std::vector<uint32_t> words(slots*4);
            for (auto& word:words) word=input.word();
            const auto constants=input.vectors<192>();
            const auto known=input.masks<16>();
            std::array<uint8_t,192> constant_known{}; constant_known.fill(15);
            std::array<uint8_t,13> observable{}; observable.fill(15); observable[5]=1;
            if (version==2) { constant_known=input.masks<192>(); observable=input.masks<13>(); }
            require(std::any_of(observable.begin(),observable.end(),[](uint8_t mask){return mask!=0;}),
                    "No observable shader outputs");
            Program program;
            require(nv2a_vsh_parse_program(&program.value,words.data(),slots)==NV2AVPR_SUCCESS,
                    "Captured shader parse failed");
            for (uint32_t vertex=0; vertex<size; ++vertex) {
                const auto values=input.vectors<16>();
                xlle::VshDependencies dependencies;
                require(admit(program,slots,observable,values,known,constants,constant_known,dependencies),
                        "Captured supported vertex failed dependency admission");
                // Counterexamples change availability only, retaining actual
                // program/value inputs. Missing consumed lanes must not pass.
                for (size_t i=0; i<dependencies.constants.size(); ++i) if (dependencies.constants[i]) {
                    auto missing=constant_known; missing[i]&=~first_lane(dependencies.constants[i]);
                    xlle::VshDependencies rejected;
                    require(!admit(program,slots,observable,values,known,constants,missing,rejected),
                            "Missing consumed constant lane was accepted");
                    ++constant_controls; break;
                }
                for (size_t i=0; i<dependencies.inputs.size(); ++i) if (dependencies.inputs[i]) {
                    auto missing=known; missing[i]&=~first_lane(dependencies.inputs[i]);
                    xlle::VshDependencies rejected;
                    require(!admit(program,slots,observable,values,missing,constants,constant_known,rejected),
                            "Missing consumed attribute lane was accepted");
                    ++input_controls; break;
                }
            }
            vertices+=size;
        }
        require(input.file.peek()==std::char_traits<char>::eof(), "Trailing captured inputs");
        require(constant_controls && input_controls, "Capture has no availability counterexamples");
        std::cout << "{\"cases\":" << count << ",\"vertices\":" << vertices
                  << ",\"missing_constant_controls\":" << constant_controls
                  << ",\"missing_attribute_controls\":" << input_controls
                  << ",\"captured_contract_passed\":true,\"complete_scene_pixel_qualified\":false}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Admission replay rejected: " << error.what() << '\n';
        return 1;
    }
}
