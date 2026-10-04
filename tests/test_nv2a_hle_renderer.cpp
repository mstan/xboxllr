#include "host/nv2a_hle_renderer.hpp"
#include "machine/nv2a.hpp"
#include "machine/nv2a_hle_draw.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace xlle;
using N = Nv2a;
static uint32_t bits(float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  return b;
}
static void require(bool ok, const std::string &why) {
  if (!ok)
    throw std::runtime_error(why);
}
int main() {
  std::string error;
  auto r = create_nv2a_hle_renderer(error);
  if (!r) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  try {
    Nv2aHleDraw p;
    p.state.resize(N::kRasterStateWords);
    p.color = {0x100000, 16,    16,    64,
               0x128,    false, false, std::vector<uint8_t>(1024)};
    p.depth = {0x200000, 16,   16,    64,
               0x128,    true, false, std::vector<uint8_t>(1024, 255)};
    p.state[N::kRasterSurfaceClipHorizontal] =
        p.state[N::kRasterSurfaceClipVertical] = 16u << 16;
    p.state[N::kRasterWindowClipHorizontal] =
        p.state[N::kRasterWindowClipVertical] = 0x0fff0000;
    p.state[N::kRasterColorMask] = 0x01010101;
    p.state[N::kRasterClipMax] = bits(16777215.f);
    p.state[N::kRasterFrontFace] = 0x900;
    p.state[N::kRasterCombinerControl] = 1;
    p.state[N::kRasterCombinerColorIcw0] = 0x04200000;
    p.state[N::kRasterCombinerAlphaIcw0] = 0x14200000;
    p.state[N::kRasterCombinerColorOcw0] =
        p.state[N::kRasterCombinerAlphaOcw0] = 0xc00;
    p.state[N::kRasterCombinerSpecularFog0] = 0xc;
    p.state[N::kRasterCombinerSpecularFog1] = 0x1c80;
    p.primitive = 5;
    for (unsigned i = 0; i < 4; ++i)
      p.constants[i][i] = 1;
    p.vertices.resize(3);
    p.vertices[0][0] = {1, 1, 100, 1};
    p.vertices[1][0] = {14, 1, 100, 1};
    p.vertices[2][0] = {1, 14, 100, 1};
    for (auto &v : p.vertices)
      v[3] = {1, 0, 0, 1};
    p.indices = {0, 1, 2};
    if (!r->submit_hle_draw(p, error))
      throw std::runtime_error(error);
    Nv2aResidentColorResolve out;
    if (!r->resolve_resident_color_surface(0x100000, 1024, out, error))
      throw std::runtime_error(error);
    require(out.data.size() == 1024 && out.data[(3 * 16 + 3) * 4 + 2] == 255,
            "fixed function triangle missing");
    p.color.bytes.clear();
    p.depth.bytes.clear();
    p.transform_mode = 2;
    // MOV oPos, v0; MOV oD0, v3. Actual NV2A instruction fields.
    p.program = {{{0, 0x0020001b, 0x08000800, 0x1000f800}},
                 {{0, 0x0020061b, 0x08000800, 0x1000f819}}};
    for (auto &v : p.vertices)
      v[3] = {0, 1, 0, 1};
    if (!r->submit_hle_draw(p, error))
      throw std::runtime_error(error);
    if (!r->resolve_resident_color_surface(0x100000, 1024, out, error))
      throw std::runtime_error(error);
    require(out.data[(3 * 16 + 3) * 4 + 1] == 255 &&
                out.data[(3 * 16 + 3) * 4 + 2] == 0,
            "GPU vertex program not executed");
    // A real failing depth test must preserve the prior pixels.
    p.state[N::kRasterEnables] = 8;
    p.state[N::kRasterDepthFunc] = 0x200;
    for (auto &v : p.vertices)
      v[3] = {0, 0, 1, 1};
    if (!r->submit_hle_draw(p, error))
      throw std::runtime_error(error);
    if (!r->resolve_resident_color_surface(0x100000, 1024, out, error))
      throw std::runtime_error(error);
    require(out.data[(3 * 16 + 3) * 4 + 1] == 255, "depth test ignored");
    // Stencil enable is machine bit 5, independently of depth testing.
    p.state[N::kRasterEnables] = 1u << 5;
    p.state[N::kRasterStencilFunc] = 0x200; // NEVER
    p.state[N::kRasterStencilFuncMask] = 255;
    p.state[N::kRasterStencilWriteMask] = 255;
    p.state[N::kRasterStencilOpFail] = p.state[N::kRasterStencilOpZFail] =
        p.state[N::kRasterStencilOpZPass] = 0x1e00; // KEEP
    if (!r->submit_hle_draw(p, error))
      throw std::runtime_error(error);
    if (!r->resolve_resident_color_surface(0x100000, 1024, out, error))
      throw std::runtime_error(error);
    require(out.data[(3 * 16 + 3) * 4 + 1] == 255 &&
                out.data[(3 * 16 + 3) * 4] == 0,
            "stencil rejection ignored");
    // Logic enable is machine bit 6: blue XOR green must produce cyan.
    p.state[N::kRasterEnables] = 1u << 6;
    p.state[N::kRasterLogicOp] = 0x1506; // XOR
    if (!r->submit_hle_draw(p, error))
      throw std::runtime_error(error);
    if (!r->resolve_resident_color_surface(0x100000, 1024, out, error))
      throw std::runtime_error(error);
    require(out.data[(3 * 16 + 3) * 4 + 1] == 255 &&
                out.data[(3 * 16 + 3) * 4] == 255,
            "logic operation ignored");
    // Rebinding an overlapping view retires the old image. Later use of
    // the old dimensions must re-upload the current guest allocation.
    auto alias = p;
    alias.color.height = 8;
    alias.color.bytes.assign(512, 0x33);
    alias.state[N::kRasterSurfaceClipVertical] = 8u << 16;
    alias.depth = {};
    alias.state[N::kRasterEnables] = 0;
    if (!r->submit_hle_draw(alias, error))
      throw std::runtime_error(error);
    require(!r->has_resident_color_surface(0x100000, 1024, 64, 16, 0x128),
            "overlapping old surface remained resident");
    require(r->has_resident_color_surface(0x100000, 512, 64, 8, 0x128),
            "new surface view missing");
    // A depth-only draw must not claim ownership of untouched color RAM.
    p.color.address = 0;
    p.color.bytes.assign(1024, 0x5a);
    p.state[N::kRasterColorMask] = 0;
    p.state[N::kRasterDepthFunc] = 0x207;
    p.state[N::kRasterDepthMask] = 1;
    if (!r->submit_hle_draw(p, error))
      throw std::runtime_error(error);
    if (!r->resolve_resident_color_surface(0, 1024, out, error))
      throw std::runtime_error(error);
    require(out.data.empty(), "depth-only draw claimed untouched color memory");
    // XRGB storage has no destination alpha channel; sampling sees 1.
    auto opaque = p;
    opaque.color = {0x300000, 16,    16,    64,
                    0x124,    false, false, std::vector<uint8_t>(1024)};
    opaque.depth = {};
    opaque.state[N::kRasterEnables] = 0;
    opaque.state[N::kRasterColorMask] = 0x01010101;
    for (auto &v : opaque.vertices)
      v[3] = {1, 0, 0, 0};
    if (!r->submit_hle_draw(opaque, error))
      throw std::runtime_error(error);
    if (!r->resolve_resident_color_surface(0x300000, 1024, out, error))
      throw std::runtime_error(error);
    require(out.data[(3 * 16 + 3) * 4 + 3] == 255,
            "XRGB acquired a writable alpha channel");
    auto rgb565 = opaque;
    rgb565.color = {0x400000, 16,    16,    32,
                    0x123,    false, false, std::vector<uint8_t>(512)};
    if (!r->submit_hle_draw(rgb565, error))
      throw std::runtime_error(error);
    if (!r->resolve_resident_color_surface(0x400000, 512, out, error))
      throw std::runtime_error(error);
    require(out.data.size() == 512 && out.data[(3 * 16 + 3) * 2] == 0 &&
                out.data[(3 * 16 + 3) * 2 + 1] == 0xf8,
            "RGB565 render target storage mismatch");
    // Freeing guest storage retires GPU ownership without a stale readback.
    if (!r->submit_hle_draw(opaque, error))
      throw std::runtime_error(error);
    r->invalidate_resident_color_surface(0x300000, 1024, 0);
    if (!r->resolve_resident_color_surface(0x300000, 1024, out, error))
      throw std::runtime_error(error);
    require(out.data.empty(), "freed allocation wrote stale GPU data");
    // Native aligned input and the unaligned-format conversion path both
    // preserve actual pixels and default attributes. Repeated large payloads
    // exercise buffer replacement and subsequent small draws exercise reuse.
    for (unsigned start : {4u, 7u}) {
      for (unsigned format : {0u, 1u, 2u, 4u, 5u, 6u}) {
        auto raw = opaque;
        raw.color.address = 0x500000;
        raw.raw_vertex_count = 3;
        raw.raw_vertices.resize(format == 0 ? 1024 * 1024 + 4 : 128);
        raw.vertex_layout[0] = {int32_t(start), 36, 2, 4};
        raw.vertex_layout[3] = {int32_t(start + 16), 36, int32_t(format),
                                format == 6 ? 1 : 4};
        for (unsigned i = 0; i < 3; ++i) {
          std::memcpy(raw.raw_vertices.data() + start + i * 36,
                      raw.vertices[i][0].data(), 16);
          auto *color = raw.raw_vertices.data() + start + 16 + i * 36;
          if (format == 0 || format == 4) {
            color[1] = 255;
            color[3] = 255;
          } else if (format == 2) {
            const float v[4] = {0, 1, 0, 1};
            std::memcpy(color, v, 16);
          } else if (format == 6) {
            const uint32_t v = 1023u << 11;
            std::memcpy(color, &v, 4);
          } else {
            const int16_t v[4] = {0, int16_t(format == 1 ? 32767 : 1), 0,
                                  int16_t(format == 1 ? 32767 : 1)};
            std::memcpy(color, v, 8);
          }
        }
        raw.vertices.clear();
        for (unsigned repetition = 0; repetition < 2; ++repetition) {
          require(r->submit_hle_draw(raw, error), error);
          require(r->resolve_resident_color_surface(0x500000, 1024, out, error),
                  error);
          require(out.data[(3 * 16 + 3) * 4 + 1] == 255 &&
                      out.data[(3 * 16 + 3) * 4 + 2] == 0,
                  "raw vertex GPU format decode mismatch: " +
                      std::to_string(format));
        }
        raw.vertex_layout[3][3] = 0;
        raw.default_attributes[3] = {0, 0, 1, 1};
        require(r->submit_hle_draw(raw, error), error);
        require(r->resolve_resident_color_surface(0x500000, 1024, out, error),
                error);
        require(out.data[(3 * 16 + 3) * 4] == 255 &&
                    out.data[(3 * 16 + 3) * 4 + 1] == 0,
                "raw vertex constant attribute lost");
        raw.indices[0] = 3;
        require(!r->submit_hle_draw(raw, error),
                "out-of-range raw index accepted");
      }
    }
    p.state[N::kRasterShaderStageProgram] =
        8; // unsupported mode, no fake success
    require(!r->submit_hle_draw(p, error),
            "unsupported shader silently accepted");
    std::puts("HLE renderer: real fixed/programmed GPU draws, depth rejection, "
              "unsupported error pass");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
