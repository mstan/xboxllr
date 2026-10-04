// Build-selected native renderer using phobos's D3D11 RHI and HLSL translators.
// Guest ownership and completion are supplied by the common NV2A device seam.
#include "host/nv2a_hle_renderer.hpp"
#include "machine/nv2a.hpp"
#include "machine/nv2a_hle_draw.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
extern "C" {
#include "third_party/norain/d3d_texture_model.h"
#include "third_party/phobos/d3d8_combiners.h"
#include "third_party/phobos/nv2a_vsh.h"
#include "third_party/phobos/rhi.h"
#include "third_party/xemu/swizzle.h"
int d3d8_vsh_generate_hlsl(const NV2AVshProgram *, char *, int);
}
namespace xlle {
namespace {
using N = Nv2a;
void require(bool ok, const std::string &why) {
  if (!ok)
    throw std::runtime_error("phobos D3D11: " + why);
}
float as_float(uint32_t n) {
  float f;
  std::memcpy(&f, &n, 4);
  return f;
}
std::array<float, 4> argb(uint32_t n) {
  return {float((n >> 16) & 255) / 255, float((n >> 8) & 255) / 255,
          float(n & 255) / 255, float(n >> 24) / 255};
}
bool overlap(uint32_t a, size_t n, uint32_t b, size_t m) {
  return uint64_t(a) < uint64_t(b) + m && uint64_t(b) < uint64_t(a) + n;
}
void replace(std::string &s, const std::string &from, const std::string &to) {
  auto at = s.find(from);
  require(at != std::string::npos, "donor shader adapter mismatch");
  s.replace(at, from.size(), to);
}
unsigned surface_bpp(const Nv2aHleSurface &s) {
  return s.depth ? (((s.format >> 4) & 15) == 1 ? 2 : 4)
                 : ((s.format & 15) <= 3 ? 2 : 4);
}
uint32_t expand_color(uint32_t value, unsigned fmt) {
  if (fmt == 1 || fmt == 2)
    return 0xff000000u | (((value >> 10) & 31) * 255 / 31 << 16) |
           (((value >> 5) & 31) * 255 / 31 << 8) | ((value & 31) * 255 / 31);
  if (fmt == 3)
    return 0xff000000u | (((value >> 11) & 31) * 255 / 31 << 16) |
           (((value >> 5) & 63) * 255 / 63 << 8) | ((value & 31) * 255 / 31);
  require(fmt == 4 || fmt == 5 || fmt == 8,
          "color format " + std::to_string(fmt));
  return fmt == 8 ? value : value | 0xff000000u;
}
uint32_t pack_color(uint32_t value, unsigned fmt) {
  if (fmt == 1 || fmt == 2)
    return 0x8000 | (((value >> 19) & 31) << 10) | (((value >> 11) & 31) << 5) |
           ((value >> 3) & 31);
  if (fmt == 3)
    return (((value >> 19) & 31) << 11) | (((value >> 10) & 63) << 5) |
           ((value >> 3) & 31);
  if (fmt == 4 || fmt == 5)
    return value | 0xff000000u;
  return value;
}
struct Image {
  RhiImage *image = nullptr;
  RhiView *output = nullptr;
  RhiView *sampled = nullptr;
  ~Image() {
    rhi_view_destroy(output);
    rhi_view_destroy(sampled);
    rhi_image_destroy(image);
  }
};
struct Plane : Image {
  Nv2aHleSurface desc;
  bool dirty = false, valid = true;
};
struct Program {
  RhiShader *vs = nullptr;
  RhiShader *ps = nullptr;
  RhiVertexLayout *layout = nullptr;
  std::map<std::string, RhiVertexLayout *> raw_layouts;
  ~Program() {
    for (auto &item : raw_layouts)
      rhi_vertex_layout_destroy(item.second);
    rhi_vertex_layout_destroy(layout);
    rhi_shader_destroy(vs);
    rhi_shader_destroy(ps);
  }
};
class PhobosRenderer final : public Nv2aRenderer {
  ID3D11Device *device_ = nullptr;
  ID3D11DeviceContext *context_ = nullptr;
  std::vector<std::unique_ptr<Plane>> planes_;
  struct Texture : Image {
    Nv2aHleTexture desc;
  };
  std::map<std::string, std::unique_ptr<Texture>> textures_;
  std::map<std::string, std::unique_ptr<Program>> programs_;
  std::map<std::string, RhiBlendState *> blends_;
  std::map<std::string, RhiDepthState *> depths_;
  std::map<std::string, RhiRasterState *> rasters_;
  std::map<std::string, RhiSampler *> samplers_;
  std::vector<RhiBuffer *> uniform_ = std::vector<RhiBuffer *>(5, nullptr);
  RhiBuffer *vertices_ = nullptr;
  RhiBuffer *indices_ = nullptr;
  size_t vertex_capacity_ = 0, index_capacity_ = 0;
  uint64_t draws_ = 0, uploads_ = 0, resolves_ = 0, raw_draws_ = 0,
           expanded_draws_ = 0;
  static size_t extent(const Plane &p) {
    return size_t(p.desc.pitch) * p.desc.height;
  }
  Plane *plane(uint32_t address, uint32_t pitch, uint32_t height,
               bool depth) const {
    for (auto &p : planes_)
      if (p->valid && p->desc.address == address && p->desc.pitch == pitch &&
          p->desc.height == height && p->desc.depth == depth)
        return p.get();
    return nullptr;
  }
  Plane &ensure_plane(const Nv2aHleSurface &s) {
    if (auto *p = plane(s.address, s.pitch, s.height, s.depth)) {
      if (p->desc.format == s.format)
        return *p;
      require(!p->dirty, "dirty target format alias");
      p->valid = false;
    }
    for (auto &p : planes_)
      if (p->valid && overlap(s.address, size_t(s.pitch) * s.height,
                              p->desc.address, extent(*p))) {
        require(!p->dirty, "dirty target alias");
        p->valid = false;
      }
    require(s.width && s.height && s.bytes.size() == size_t(s.pitch) * s.height,
            "missing target contents");
    const unsigned bpp = surface_bpp(s);
    std::vector<uint8_t> linear(size_t(s.width) * s.height * bpp);
    if (s.swizzled)
      require(recomp_d3d_texture_unswizzle(s.bytes.data(), linear.data(),
                                           s.width, s.height, bpp),
              "target unswizzle");
    else
      for (unsigned y = 0; y < s.height; ++y)
        std::memcpy(linear.data() + size_t(y) * s.width * bpp,
                    s.bytes.data() + size_t(y) * s.pitch,
                    size_t(s.width) * bpp);
    auto p = std::make_unique<Plane>();
    p->desc = s;
    RhiImageDesc d{};
    d.width = s.width;
    d.height = s.height;
    d.depth = 1;
    d.mip_levels = 1;
    d.samples = 1;
    d.bind =
        (s.depth ? RHI_BIND_DEPTH : RHI_BIND_RENDER_TARGET) | RHI_BIND_SAMPLED;
    d.format = s.depth ? (bpp == 2 ? RHI_FORMAT_D16_UNORM
                                   : RHI_FORMAT_D24_UNORM_S8_UINT)
                       : ((s.format & 15) == 8 ? RHI_FORMAT_B8G8R8A8_UNORM
                                               : RHI_FORMAT_B8G8R8X8_UNORM);
    std::vector<uint8_t> gpu(size_t(s.width) * s.height * (s.depth ? bpp : 4));
    for (size_t i = 0; i < size_t(s.width) * s.height; ++i) {
      uint32_t word = 0;
      std::memcpy(&word, linear.data() + i * bpp, bpp);
      if (!s.depth)
        word = expand_color(word, s.format & 15);
      else if (bpp == 4)
        word = (word >> 8) | (word << 24);
      std::memcpy(gpu.data() + i * (s.depth ? bpp : 4), &word,
                  s.depth ? bpp : 4);
    }
    RhiSubresourceData init{gpu.data(), s.width * (s.depth ? bpp : 4), 0};
    p->image = rhi_image_create(&d, &init);
    require(p->image, "target image creation");
    p->output = rhi_view_create(
        p->image, s.depth ? RHI_VIEW_DEPTH : RHI_VIEW_RENDER_TARGET, nullptr);
    require(p->output, "target view");
    p->sampled = rhi_view_create(p->image, RHI_VIEW_SAMPLED, nullptr);
    require(p->sampled, "target texture view");
    auto *result = p.get();
    ++uploads_;
    for (auto &old : planes_)
      if (!old->valid) {
        old = std::move(p);
        return *result;
      }
    planes_.push_back(std::move(p));
    return *result;
  }
  void read_plane(Plane &p, std::vector<uint8_t> &out) {
    const auto &s = p.desc;
    unsigned bpp = surface_bpp(s), gpu_bpp = s.depth ? bpp : 4;
    std::vector<uint8_t> gpu(size_t(s.width) * s.height * gpu_bpp),
        linear(size_t(s.width) * s.height * bpp);
    require(!rhi_image_readback(p.image, 0, gpu.data(), s.width * gpu_bpp),
            "target GPU readback");
    for (size_t i = 0; i < size_t(s.width) * s.height; ++i) {
      uint32_t word = 0;
      std::memcpy(&word, gpu.data() + i * gpu_bpp, gpu_bpp);
      if (!s.depth)
        word = pack_color(word, s.format & 15);
      else if (bpp == 4)
        word = (word << 8) | (word >> 24);
      std::memcpy(linear.data() + i * bpp, &word, bpp);
    }
    out = s.bytes;
    if (s.swizzled) {
      uint32_t mx = 0, my = 0;
      recomp_d3d_texture_swizzle_masks(s.width, s.height, &mx, &my);
      for (unsigned y = 0; y < s.height; ++y)
        for (unsigned x = 0; x < s.width; ++x)
          std::memcpy(
              out.data() +
                  size_t(recomp_d3d_texture_swizzle_offset(x, y, mx, my)) * bpp,
              linear.data() + (size_t(y) * s.width + x) * bpp, bpp);
    } else
      for (unsigned y = 0; y < s.height; ++y)
        std::memcpy(out.data() + size_t(y) * s.pitch,
                    linear.data() + size_t(y) * s.width * bpp,
                    size_t(s.width) * bpp);
    p.desc.bytes = out;
    p.dirty = false;
    ++resolves_;
  }
  RhiView *texture(const Nv2aHleTexture &t);
  static NV2ACombinerInput input(uint32_t n) {
    return {NV2ACombinerRegister(n & 15), int((n >> 4) & 1),
            NV2AInputMapping((n >> 5) & 7)};
  }
  static NV2ACombinerOutput output(uint32_t n) {
    return {NV2ACombinerRegister((n >> 4) & 15),
            NV2ACombinerRegister(n & 15),
            NV2ACombinerRegister((n >> 8) & 15),
            int((n >> 13) & 1),
            int((n >> 12) & 1),
            int((n >> 14) & 1),
            NV2AOutputMapping((n >> 15) & 7),
            int((n >> 19) & 1),
            int((n >> 18) & 1)};
  }
  static NV2ACombinerState combiner(const Nv2aHleDraw &p) {
    const auto &r = p.state;
    NV2ACombinerState s{};
    s.num_stages = r[N::kRasterCombinerControl] & 15;
    require(s.num_stages <= 8, "combiner stage count");
    s.count_flags = NV2A_COUNT_KNOWN | (r[N::kRasterCombinerControl] >> 8);
    s.final_flags = r[N::kRasterCombinerSpecularFog1] & 255;
    for (unsigned i = 0; i < 8; ++i) {
      for (unsigned j = 0; j < 4; ++j) {
        s.stages[i].rgb_input[j] =
            input(r[N::kRasterCombinerColorIcw0 + i] >> (24 - j * 8));
        s.stages[i].alpha_input[j] =
            input(r[N::kRasterCombinerAlphaIcw0 + i] >> (24 - j * 8));
      }
      s.stages[i].rgb_output = output(r[N::kRasterCombinerColorOcw0 + i]);
      s.stages[i].alpha_output = output(r[N::kRasterCombinerAlphaOcw0 + i]);
    }
    for (unsigned i = 0; i < 7; ++i)
      s.final_input[i] = input(r[i < 4 ? N::kRasterCombinerSpecularFog0
                                       : N::kRasterCombinerSpecularFog1] >>
                               (24 - (i % 4) * 8));
    for (unsigned i = 0; i < 4; ++i) {
      s.xmode[i] = (r[N::kRasterShaderStageProgram] >> (i * 5)) & 31;
      require(s.xmode[i] <= 4 || s.xmode[i] == 15 || s.xmode[i] == 16,
              "texture shader mode requires additional donor adapter: " +
                  std::to_string(s.xmode[i]));
      s.input_tex[i] =
          i < 2 ? 0
                : ((r[N::kRasterShaderOtherStageInput] >> (16 + (i - 2) * 4)) &
                   15);
      if (s.xmode[i] == 15 || s.xmode[i] == 16)
        require(i && s.input_tex[i] < i, "dependent texture stage source");
      s.tex_mode[i] = !p.textures[i].present          ? NV2A_TEXMODE_NONE
                      : p.textures[i].cubemap         ? NV2A_TEXMODE_CUBEMAP
                      : p.textures[i].dimensions == 3 ? NV2A_TEXMODE_3D
                                                      : NV2A_TEXMODE_2D;
      s.shadow[i] = s.xmode[i] && (p.textures[i].format == 0x30 ||
                                   p.textures[i].format == 0x2c ||
                                   p.textures[i].format == 0x2e);
      require(!(r[N::kRasterTextureControl0_0 + i] & 4),
              "texture alpha-kill not yet adapted");
    }
    // The shared packet does not currently export the NV2A fog-color word.
    // Alpha-only fog inputs use the interpolated factor; RGB needs that word.
    for (int i = 0; i < s.num_stages; ++i)
      for (const auto &in : s.stages[i].rgb_input)
        require(in.reg != NV2A_REG_FOG || in.alpha_rep,
                "fog RGB requires packet contract extension");
    for (unsigned i = 0; i < 6; ++i)
      require(s.final_input[i].reg != NV2A_REG_FOG ||
                  s.final_input[i].alpha_rep,
              "final fog RGB requires packet contract extension");
    return s;
  }
  Program &program(const Nv2aHleDraw &p) {
    auto s = combiner(p);
    std::string key(reinterpret_cast<const char *>(&s),
                    NV2A_COMBINER_KEY_BYTES);
    key.append(reinterpret_cast<const char *>(&p.transform_mode), 4);
    if (!p.program.empty())
      key.append(reinterpret_cast<const char *>(p.program.data()),
                 p.program.size() * 16);
    auto found = programs_.find(key);
    if (found != programs_.end())
      return *found->second;
    std::string vs;
    std::vector<char> generated(256 * 1024);
    if (p.transform_mode == 2) {
      require(!p.program.empty() && p.program.size() <= 136 &&
                  (p.program.back()[3] & 1),
              "vertex program extent");
      for (const auto &op : p.program)
        require(((op[1] >> 21) & 15) <= 13 &&
                    (!(op[3] & 0xf000) || (op[3] & 0x800)),
                "unsupported vertex instruction");
      NV2AVshProgram parsed{};
      nv2a_vsh_parse(p.program[0].data(), int(p.program.size()), &parsed);
      const int n = d3d8_vsh_generate_hlsl(&parsed, generated.data(),
                                           int(generated.size()));
      require(n > 0 && size_t(n) < generated.size(), "vertex HLSL generation");
      vs.assign(generated.data(), n);
      const auto begin =
          vs.find("    /* Undo the Xbox screen-space transform */");
      const auto end = vs.find("    /* Write outputs */", begin);
      require(begin != std::string::npos && end != std::string::npos,
              "vertex screen adapter");
      vs.replace(begin, end - begin,
                 "    "
                 "oPos.xy=float2(2*oPos.x/xboxScreenspaceScale.x-1,1-2*oPos.y/"
                 "xboxScreenspaceScale.y)*oPos.w;\n    "
                 "oPos.z=oPos.z/xboxScreenspaceScale.z*oPos.w;\n");
    } else {
      require(p.transform_mode == 0, "unsupported vertex mode");
      vs = R"(cbuffer VSH_Constants:register(b1){float4 c[192];};
cbuffer Screen:register(b2){float4 screen;float4 ambient;float4 options;};
cbuffer VertexDefaults:register(b3){float4 xboxVertexData[16];float4 xboxVertexFed[4];};
struct VS_IN{float4 v0:ATTR0;float4 v3:ATTR3;float4 v4:ATTR4;float4 v9:ATTR9;float4 v10:ATTR10;float4 v11:ATTR11;float4 v12:ATTR12;};
struct VS_OUT{float4 pos:SV_POSITION;float4 d0:COLOR0;float4 d1:COLOR1;float4 t0:TEXCOORD0;float4 t1:TEXCOORD1;float4 t2:TEXCOORD2;float4 t3:TEXCOORD3;float fog:TEXCOORD4;};
VS_OUT main(VS_IN input){VS_OUT o;
float4 v0=xboxVertexFed[0][0]!=0?input.v0:xboxVertexData[0];
float4 v3=xboxVertexFed[0][3]!=0?input.v3:xboxVertexData[3];
float4 v4=xboxVertexFed[1][0]!=0?input.v4:xboxVertexData[4];
float4 v9=xboxVertexFed[2][1]!=0?input.v9:xboxVertexData[9];
float4 v10=xboxVertexFed[2][2]!=0?input.v10:xboxVertexData[10];
float4 v11=xboxVertexFed[2][3]!=0?input.v11:xboxVertexData[11];
float4 v12=xboxVertexFed[3][0]!=0?input.v12:xboxVertexData[12];float4 pos=float4(dot(v0,c[0]),dot(v0,c[1]),dot(v0,c[2]),dot(v0,c[3]));
pos.w=pos.w>=0?clamp(pos.w,1e-19,1e19):clamp(pos.w,-1e19,-1e-19);pos.xyz/=pos.w;
pos.xy=trunc((pos.xy+c[59].xy)*16)/16;
o.pos=float4((2*pos.x/screen.x-1)*pos.w,(1-2*pos.y/screen.y)*pos.w,pos.z/screen.z*pos.w,pos.w);
o.d0=saturate(options.x!=0?ambient:v3);o.d1=float4(0,0,0,(options.x!=0&&options.y!=0)?v4.w:1);
o.t0=v9;o.t1=v10;o.t2=v11;o.t3=v12;o.fog=0;return o;})";
    }
    const int n = d3d8_combiners_generate_hlsl(&s, generated.data(),
                                               int(generated.size()));
    require(n > 0 && size_t(n) < generated.size(), "pixel HLSL generation");
    auto prog = std::make_unique<Program>();
    char error[8192]{};
    RhiShaderSource vsrc{vs.data(), vs.size(), "nv2a-vs", nullptr,
                         "main",    "vs_5_0",  1};
    RhiShaderSource psrc{generated.data(), size_t(n), "nv2a-ps", nullptr,
                         "main",           "ps_5_0",  1};
    prog->vs = rhi_shader_create(RHI_STAGE_VERTEX, &vsrc, error, sizeof(error));
    require(prog->vs, error);
    prog->ps = rhi_shader_create(RHI_STAGE_PIXEL, &psrc, error, sizeof(error));
    require(prog->ps, error);
    std::array<RhiVertexElement, 16> elements{};
    for (unsigned i = 0; i < 16; ++i)
      elements[i] = {"ATTR", i, RHI_FORMAT_R32G32B32A32_FLOAT, 0, i * 16};
    prog->layout = rhi_vertex_layout_create(elements.data(), 16, prog->vs);
    require(prog->layout, "vertex layout");
    auto *result = prog.get();
    programs_.emplace(std::move(key), std::move(prog));
    return *result;
  }
  void uniform(unsigned slot, unsigned stage, unsigned binding,
               const void *data, size_t size) {
    if (!uniform_[slot]) {
      RhiBufferDesc d{uint32_t(size), RHI_USAGE_DEFAULT, RHI_BIND_UNIFORM, 0};
      uniform_[slot] = rhi_buffer_create(&d, nullptr);
      require(uniform_[slot], "uniform buffer");
    }
    rhi_buffer_update(uniform_[slot], data);
    rhi_set_uniform_buffers(stage, binding, 1, &uniform_[slot]);
  }
  void stream(RhiBuffer *&buffer, size_t &capacity, unsigned bind,
              const void *data, size_t size) {
    require(size && size <= UINT32_MAX, "stream extent");
    if (size > capacity) {
      rhi_buffer_destroy(buffer);
      capacity = std::max(size, size_t(1024 * 1024));
      RhiBufferDesc d{uint32_t(capacity), RHI_USAGE_DYNAMIC, bind,
                      RHI_CPU_WRITE};
      buffer = rhi_buffer_create(&d, nullptr);
      require(buffer, "stream buffer");
    }
    auto *mapped = rhi_buffer_map(buffer, RHI_MAP_WRITE_DISCARD);
    require(mapped, "stream map");
    std::memcpy(mapped, data, size);
    rhi_buffer_unmap(buffer);
  }
  static uint32_t blend(uint32_t v) {
    switch (v) {
    case 0:
      return RHI_BLEND_ZERO;
    case 1:
      return RHI_BLEND_ONE;
    case 0x300:
      return RHI_BLEND_SRC_COLOR;
    case 0x301:
      return RHI_BLEND_INV_SRC_COLOR;
    case 0x302:
      return RHI_BLEND_SRC_ALPHA;
    case 0x303:
      return RHI_BLEND_INV_SRC_ALPHA;
    case 0x304:
      return RHI_BLEND_DEST_ALPHA;
    case 0x305:
      return RHI_BLEND_INV_DEST_ALPHA;
    case 0x306:
      return RHI_BLEND_DEST_COLOR;
    case 0x307:
      return RHI_BLEND_INV_DEST_COLOR;
    case 0x308:
      return RHI_BLEND_SRC_ALPHA_SAT;
    case 0x8001:
    case 0x8003:
      return RHI_BLEND_FACTOR;
    case 0x8002:
    case 0x8004:
      return RHI_BLEND_INV_FACTOR;
    default:
      throw std::runtime_error("phobos D3D11: blend factor");
    }
  }
  static uint32_t stencil(uint32_t v) {
    switch (v) {
    case 0:
      return RHI_STENCIL_ZERO;
    case 0x1e00:
      return RHI_STENCIL_KEEP;
    case 0x1e01:
      return RHI_STENCIL_REPLACE;
    case 0x1e02:
      return RHI_STENCIL_INCR_SAT;
    case 0x1e03:
      return RHI_STENCIL_DECR_SAT;
    case 0x150a:
      return RHI_STENCIL_INVERT;
    case 0x8507:
      return RHI_STENCIL_INCR;
    case 0x8508:
      return RHI_STENCIL_DECR;
    default:
      throw std::runtime_error("phobos D3D11: stencil operation");
    }
  }
  template <class T, class H, class F>
  H *cached(std::map<std::string, H *> &cache, const T &desc, F create) {
    std::string key(reinterpret_cast<const char *>(&desc), sizeof(desc));
    auto &result = cache[key];
    if (!result) {
      result = create(&desc);
      require(result, "state creation");
    }
    return result;
  }
  bool resident(bool depth, uint32_t a, size_t n, uint32_t pitch, uint32_t h,
                uint32_t fmt) const {
    auto *p = plane(a, pitch, h, depth);
    return p && extent(*p) == n && (!fmt || p->desc.format == fmt);
  }
  template <class Out>
  bool resolve(bool depth, uint32_t a, size_t n, Out &out, std::string &error) {
    out = {};
    try {
      for (auto &p : planes_)
        if (p->valid && p->dirty && p->desc.depth == depth &&
            overlap(a, n, p->desc.address, extent(*p))) {
          out.address = p->desc.address;
          read_plane(*p, out.data);
          break;
        }
      return true;
    } catch (const std::exception &e) {
      error = e.what();
      return false;
    }
  }

public:
  bool initialize(std::string &error) {
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   0, nullptr, 0, D3D11_SDK_VERSION, &device_,
                                   nullptr, &context_);
    if (FAILED(hr)) {
      error = "D3D11CreateDevice failed";
      return false;
    }
    require(!rhi_d3d11_adopt(device_, context_), "RHI adoption");
    return true;
  }
  ~PhobosRenderer() override {
    std::printf("[phobos-cache] planes=%zu textures=%zu programs=%zu blend=%zu "
                "depth=%zu raster=%zu samplers=%zu\n",
                planes_.size(), textures_.size(), programs_.size(),
                blends_.size(), depths_.size(), rasters_.size(),
                samplers_.size());
    if (context_) {
      context_->ClearState();
      context_->Flush();
    }
    planes_.clear();
    textures_.clear();
    programs_.clear();
    for (auto &p : blends_)
      rhi_blend_state_destroy(p.second);
    for (auto &p : depths_)
      rhi_depth_state_destroy(p.second);
    for (auto &p : rasters_)
      rhi_raster_state_destroy(p.second);
    for (auto &p : samplers_)
      rhi_sampler_destroy(p.second);
    for (auto *p : uniform_)
      rhi_buffer_destroy(p);
    rhi_buffer_destroy(vertices_);
    rhi_buffer_destroy(indices_);
    rhi_device_destroy();
    std::printf("[phobos-renderer] draws=%llu uploads=%llu resolves=%llu "
                "raw-draws=%llu expanded-draws=%llu\n",
                (unsigned long long)draws_, (unsigned long long)uploads_,
                (unsigned long long)resolves_, (unsigned long long)raw_draws_,
                (unsigned long long)expanded_draws_);
  }
  const char *name() const override { return "hle-phobos-d3d11"; }
  bool
  supports_generic_combiner(const Nv2aGenericCombinerPacket &) const override {
    return false;
  }
  bool submit_generic_combiner(const Nv2aGenericCombinerPacket &,
                               Nv2aGenericCombinerResult &,
                               std::string &e) override {
    e = "LLE packet submitted to phobos HLE renderer";
    return false;
  }
  bool requires_resident_target_switch_resolve() const override {
    return false;
  }
  bool has_resident_color_surface(uint32_t a, size_t n, uint32_t p, uint32_t h,
                                  uint32_t f) const override {
    return resident(false, a, n, p, h, f);
  }
  bool has_resident_depth_surface(uint32_t a, size_t n, uint32_t p, uint32_t h,
                                  uint32_t f) const override {
    return resident(true, a, n, p, h, f);
  }
  bool resolve_resident_color_surface(uint32_t a, size_t n,
                                      Nv2aResidentColorResolve &o,
                                      std::string &e) override {
    return resolve(false, a, n, o, e);
  }
  bool resolve_resident_depth_surface(uint32_t a, size_t n,
                                      Nv2aResidentDepthResolve &o,
                                      std::string &e) override {
    return resolve(true, a, n, o, e);
  }
  void invalidate_resident_color_surface(uint32_t a, size_t n,
                                         uint32_t) override {
    for (auto &p : planes_)
      if (p->valid && !p->desc.depth &&
          overlap(a, n, p->desc.address, extent(*p)))
        p->valid = false;
  }
  void invalidate_resident_depth_surface(uint32_t a, size_t n,
                                         uint32_t) override {
    for (auto &p : planes_)
      if (p->valid && p->desc.depth &&
          overlap(a, n, p->desc.address, extent(*p)))
        p->valid = false;
  }
  void kick_pending_gpu_work() override { context_->Flush(); }
  bool clear_resident_surface(uint32_t ca, size_t cn, bool cc, uint32_t cv,
                              uint32_t da, size_t dn, bool cd, uint32_t dv,
                              bool &fill, std::string &error) override {
    Plane *c = nullptr, *d = nullptr;
    for (auto &p : planes_)
      if (p->valid) {
        if (!p->desc.depth && p->desc.address == ca && extent(*p) == cn)
          c = p.get();
        if (p->desc.depth && p->desc.address == da && extent(*p) == dn)
          d = p.get();
      }
    if ((cc && !c) || (cd && !d))
      return false;
    try {
      fill = (cc && !c->dirty) || (cd && !d->dirty);
      if (cc) {
        auto color = argb(expand_color(cv, c->desc.format & 15));
        rhi_clear_color(c->output, color.data());
      }
      if (cd) {
        bool z16 = surface_bpp(d->desc) == 2;
        rhi_clear_depth(
            d->output, RHI_CLEAR_DEPTH | (z16 ? 0 : RHI_CLEAR_STENCIL),
            z16 ? float(dv & 65535) / 65535 : float(dv >> 8) / 16777215,
            uint8_t(dv));
      }
      require(SUCCEEDED(device_->GetDeviceRemovedReason()),
              "GPU clear failure");
      return true;
    } catch (const std::exception &e) {
      error = e.what();
      return false;
    }
  }
  bool submit_hle_draw(const Nv2aHleDraw &p, std::string &error) override;
};

RhiView *PhobosRenderer::texture(const Nv2aHleTexture &t) {
  if (t.resident) {
    auto *p = plane(t.address, t.pitch, t.height,
                    t.format == 0x30 || t.format == 0x2c || t.format == 0x2e);
    require(p && p->sampled, "resident texture needs compatible sampled view");
    return p->sampled;
  }
  std::string key = std::to_string(t.address) + ":" + std::to_string(t.width) +
                    ":" + std::to_string(t.height) + ":" +
                    std::to_string(t.depth) + ":" + std::to_string(t.format) +
                    ":" + std::to_string(t.levels) + ":" +
                    std::to_string(t.cubemap);
  auto found = textures_.find(key);
  if (found != textures_.end() && found->second->desc.bytes == t.bytes &&
      found->second->desc.palette == t.palette)
    return found->second->sampled;
  bool bc = t.format == 12 || t.format == 14 || t.format == 15;
  bool depth = t.format == 0x30 || t.format == 0x2c || t.format == 0x2e;
  require(!bc || t.depth == 1, "compressed volume texture");
  unsigned bpp = 4;
  switch (t.format) {
  case 0:
  case 0x19:
  case 0xb:
    bpp = 1;
    break;
  case 2:
  case 3:
  case 4:
  case 5:
  case 0x10:
  case 0x11:
  case 0x30:
  case 0x2c:
    bpp = 2;
    break;
  case 6:
  case 7:
  case 0x12:
  case 0x1e:
  case 0x2e:
  case 12:
  case 14:
  case 15:
    break;
  default:
    throw std::runtime_error("phobos D3D11: texture format requires adapter: " +
                             std::to_string(t.format));
  }
  RhiImageDesc d{};
  d.type = t.dimensions == 3 ? RHI_IMAGE_3D : RHI_IMAGE_2D;
  d.width = t.width;
  d.height = t.height;
  d.depth = t.cubemap ? 6 : t.depth;
  d.mip_levels = t.levels;
  d.samples = 1;
  d.bind = RHI_BIND_SAMPLED;
  d.cube = t.cubemap;
  d.format = bc ? (t.format == 12   ? RHI_FORMAT_BC1_UNORM
                   : t.format == 14 ? RHI_FORMAT_BC2_UNORM
                                    : RHI_FORMAT_BC3_UNORM)
                : RHI_FORMAT_B8G8R8A8_UNORM;
  if (depth)
    d.format = RHI_FORMAT_R32_FLOAT;
  std::vector<std::vector<uint8_t>> data;
  std::vector<RhiSubresourceData> initial;
  size_t offset = 0;
  for (unsigned face = 0; face < (t.cubemap ? 6u : 1u); ++face) {
    unsigned w = t.width, h = t.height, z = t.depth;
    for (unsigned level = 0; level < t.levels; ++level) {
      size_t bytes =
          bc ? size_t((w + 3) / 4) * ((h + 3) / 4) * (t.format == 12 ? 8 : 16)
             : (t.linear ? size_t(t.pitch) * h * z : size_t(w) * h * z * bpp);
      require(offset + bytes <= t.bytes.size(), "texture mip bounds");
      uint32_t pitch = bc ? ((w + 3) / 4) * (t.format == 12 ? 8 : 16) : w * 4;
      if (bc)
        data.emplace_back(t.bytes.begin() + offset,
                          t.bytes.begin() + offset + bytes);
      else {
        std::vector<uint8_t> linear(size_t(w) * h * z * bpp),
            rgba(size_t(w) * h * z * 4);
        if (t.linear) {
          for (unsigned slice = 0; slice < z; ++slice)
            for (unsigned y = 0; y < h; ++y)
              std::memcpy(linear.data() + (size_t(slice) * h + y) * w * bpp,
                          t.bytes.data() + offset +
                              (size_t(slice) * h + y) * t.pitch,
                          size_t(w) * bpp);
        } else if (z > 1)
          unswizzle_box(t.bytes.data() + offset, w, h, z, linear.data(),
                        w * bpp, w * h * bpp, bpp);
        else
          require(recomp_d3d_texture_unswizzle(t.bytes.data() + offset,
                                               linear.data(), w, h, bpp),
                  "texture unswizzle");
        for (size_t i = 0; i < size_t(w) * h * z; ++i) {
          uint32_t c = 0;
          std::memcpy(&c, linear.data() + i * bpp, bpp);
          if (depth) {
            float value = t.format == 0x2e ? float(c >> 8) / 16777215.f
                                           : float(c) / 65535.f;
            std::memcpy(rgba.data() + i * 4, &value, 4);
            continue;
          }
          switch (t.format) {
          case 6:
          case 0x12:
            break;
          case 7:
          case 0x1e:
            c |= 0xff000000;
            break;
          case 0:
            c = 0xff000000 | (c * 0x10101);
            break;
          case 0x19:
            c = (c << 24) | 0xffffff;
            break;
          case 0xb:
            require(size_t(c) * 4 + 4 <= t.palette.size(), "palette extent");
            std::memcpy(&c, t.palette.data() + c * 4, 4);
            break;
          case 5:
          case 0x11:
            c = expand_color(c, 3);
            break;
          case 3:
            c = expand_color(c, 1);
            break;
          case 2:
          case 0x10: {
            unsigned alpha = c >> 15;
            c = (expand_color(c, 1) & 0xffffff) | (alpha ? 0xff000000 : 0);
            break;
          }
          case 4:
            c = ((c >> 12) * 17 << 24) | (((c >> 8) & 15) * 17 << 16) |
                (((c >> 4) & 15) * 17 << 8) | ((c & 15) * 17);
            break;
          }
          std::memcpy(rgba.data() + i * 4, &c, 4);
        }
        data.push_back(std::move(rgba));
      }
      initial.push_back(
          {data.back().data(), pitch, bc ? pitch * ((h + 3) / 4) : pitch * h});
      offset += bytes;
      w = std::max(1u, w / 2);
      h = std::max(1u, h / 2);
      z = std::max(1u, z / 2);
    }
    if (t.cubemap)
      offset = (offset + 127) & ~size_t(127);
  }
  auto result = std::make_unique<Texture>();
  result->desc = t;
  result->image = rhi_image_create(&d, initial.data());
  require(result->image, "texture image creation");
  result->sampled = rhi_view_create(result->image, RHI_VIEW_SAMPLED, nullptr);
  require(result->sampled, "texture sampled view");
  auto *view = result->sampled;
  textures_[key] = std::move(result);
  ++uploads_;
  return view;
}

bool PhobosRenderer::submit_hle_draw(const Nv2aHleDraw &p, std::string &error) {
  try {
    require(p.state.size() == N::kRasterStateWords, "raster state extent");
    if (p.indices.empty())
      return true; // An actual empty primitive has no pixels.
    bool native_raw = p.raw_vertex_count != 0;
    std::array<uint32_t, 16> formats{};
    if (native_raw)
      for (unsigned a = 0; a < 16; ++a) {
        const auto &d = p.vertex_layout[a];
        if (!d[3])
          continue;
        unsigned format = 0, bpp = 1;
        if (d[2] == 2) {
          const unsigned f[4] = {RHI_FORMAT_R32_FLOAT, RHI_FORMAT_R32G32_FLOAT,
                                 RHI_FORMAT_R32G32B32_FLOAT,
                                 RHI_FORMAT_R32G32B32A32_FLOAT};
          if (d[3] >= 1 && d[3] <= 4)
            format = f[d[3] - 1];
          bpp = 4;
        } else if (d[2] == 0 || d[2] == 4) {
          if (d[3] == 1)
            format = RHI_FORMAT_R8_UNORM;
          if (d[3] == 2)
            format = RHI_FORMAT_R8G8_UNORM;
          if (d[3] == 4)
            format = RHI_FORMAT_R8G8B8A8_UNORM;
        } else if (d[2] == 1) {
          if (d[3] == 1)
            format = RHI_FORMAT_R16_SNORM;
          if (d[3] == 2)
            format = RHI_FORMAT_R16G16_SNORM;
          if (d[3] == 4)
            format = RHI_FORMAT_R16G16B16A16_SNORM;
          bpp = 2;
        }
        if (!format || d[0] < 0 || d[1] < 0 || d[0] % bpp || d[1] % bpp) {
          native_raw = false;
          break;
        }
        require(uint64_t(d[0]) + uint64_t(d[1]) * (p.raw_vertex_count - 1) +
                        bpp * d[3] <=
                    p.raw_vertices.size(),
                "raw vertex bounds");
        formats[a] = format;
      }
    auto &prog = program(p);
    auto &color = ensure_plane(p.color);
    auto *depth = p.depth.width ? &ensure_plane(p.depth) : nullptr;
    rhi_set_render_target(color.output, depth ? depth->output : nullptr);
    const auto &r = p.state;
    unsigned enables = r[N::kRasterEnables];
    RhiViewport viewport{0, 0, float(p.color.width), float(p.color.height),
                         0, 1};
    rhi_set_viewports(1, &viewport);
    int x = r[N::kRasterSurfaceClipHorizontal] & 65535,
        y = r[N::kRasterSurfaceClipVertical] & 65535;
    int right = x + (r[N::kRasterSurfaceClipHorizontal] >> 16),
        bottom = y + (r[N::kRasterSurfaceClipVertical] >> 16);
    x = std::max(x, int(r[N::kRasterWindowClipHorizontal] & 4095));
    y = std::max(y, int(r[N::kRasterWindowClipVertical] & 4095));
    right = std::min(right,
                     int((r[N::kRasterWindowClipHorizontal] >> 16) & 4095) + 1);
    bottom = std::min(bottom,
                      int((r[N::kRasterWindowClipVertical] >> 16) & 4095) + 1);
    RhiRect scissor{x, y, std::max(x, right), std::max(y, bottom)};
    rhi_set_scissor(&scissor);
    RhiRasterDesc raster{};
    raster.fill = RHI_FILL_SOLID;
    raster.cull =
        (enables & 4)
            ? (r[N::kRasterCullFace] == 0x404 ? RHI_CULL_FRONT : RHI_CULL_BACK)
            : RHI_CULL_NONE;
    raster.front_ccw = r[N::kRasterFrontFace] == 0x901;
    raster.depth_clip = 1;
    raster.scissor = 1;
    if (enables & (1 << 13)) {
      raster.depth_bias = int32_t(as_float(r[N::kRasterPolygonOffsetBias]));
      raster.slope_scaled_depth_bias =
          as_float(r[N::kRasterPolygonOffsetScale]);
    }
    rhi_set_raster_state(cached(rasters_, raster, rhi_raster_state_create));
    RhiDepthDesc ds{};
    ds.depth_enable = depth && (enables & 8);
    ds.depth_write = depth && r[N::kRasterDepthMask];
    ds.depth_func = (r[N::kRasterDepthFunc] & 7) + 1;
    ds.stencil_enable = depth && (enables & 32);
    ds.stencil_read_mask = uint8_t(r[N::kRasterStencilFuncMask]);
    ds.stencil_write_mask = uint8_t(r[N::kRasterStencilWriteMask]);
    ds.front = {stencil(r[N::kRasterStencilOpFail]),
                stencil(r[N::kRasterStencilOpZFail]),
                stencil(r[N::kRasterStencilOpZPass]),
                (r[N::kRasterStencilFunc] & 7) + 1};
    ds.back = ds.front;
    rhi_set_depth_state(cached(depths_, ds, rhi_depth_state_create),
                        r[N::kRasterStencilRef]);
    RhiBlendDesc bs{};
    bs.enable = (enables & 2) != 0;
    bs.src = blend(r[N::kRasterBlendSrc]);
    bs.dst = blend(r[N::kRasterBlendDst]);
    if (enables & 64) {
      constexpr uint32_t logic_ops[16] = {0, 6,  12, 2,  13, 4,  10, 8,
                                          9, 11, 5,  14, 3,  15, 7,  1};
      const uint32_t operation = r[N::kRasterLogicOp];
      require(operation >= 0x1500 && operation <= 0x150f,
              "logic operation value");
      bs.logic_enable = 1;
      bs.logic_op = logic_ops[operation - 0x1500];
    }
    switch (r[N::kRasterBlendEquation]) {
    case 0:
    case 0x8006:
      bs.op = RHI_BLEND_OP_ADD;
      break;
    case 0x8007:
      bs.op = RHI_BLEND_OP_MIN;
      break;
    case 0x8008:
      bs.op = RHI_BLEND_OP_MAX;
      break;
    case 0x800a:
      bs.op = RHI_BLEND_OP_SUBTRACT;
      break;
    case 0x800b:
      bs.op = RHI_BLEND_OP_REV_SUBTRACT;
      break;
    default:
      throw std::runtime_error("phobos D3D11: blend equation");
    }
    auto alpha_factor = [](unsigned f) {
      if (f == RHI_BLEND_SRC_COLOR)
        return unsigned(RHI_BLEND_SRC_ALPHA);
      if (f == RHI_BLEND_INV_SRC_COLOR)
        return unsigned(RHI_BLEND_INV_SRC_ALPHA);
      if (f == RHI_BLEND_DEST_COLOR)
        return unsigned(RHI_BLEND_DEST_ALPHA);
      if (f == RHI_BLEND_INV_DEST_COLOR)
        return unsigned(RHI_BLEND_INV_DEST_ALPHA);
      return f;
    };
    bs.src_alpha = alpha_factor(bs.src);
    bs.dst_alpha = alpha_factor(bs.dst);
    bs.op_alpha = bs.op;
    uint32_t mask = r[N::kRasterColorMask];
    bs.write_mask = ((mask & 0x10000) ? 1 : 0) | ((mask & 0x100) ? 2 : 0) |
                    ((mask & 1) ? 4 : 0) | ((mask & 0x1000000) ? 8 : 0);
    auto blend_color = argb(r[N::kRasterBlendColor]);
    rhi_set_blend_state(cached(blends_, bs, rhi_blend_state_create),
                        blend_color.data(), UINT32_MAX);
    rhi_set_shader(RHI_STAGE_VERTEX, prog.vs);
    rhi_set_shader(RHI_STAGE_PIXEL, prog.ps);
    rhi_set_vertex_layout(prog.layout);
    uniform(0, RHI_STAGE_VERTEX, 1, p.constants.data(), sizeof(p.constants));
    std::array<std::array<float, 4>, 3> screen{
        {{float(p.color.width), float(p.color.height),
          ((p.color.format >> 4) & 15) == 1 ? 65535.f : 16777215.f, 1},
         p.ambient_color,
         {float(p.ambient_lighting), float(p.specular_alpha_from_vertex), 0,
          0}}};
    uniform(1, RHI_STAGE_VERTEX, 2, screen.data(), sizeof(screen));
    std::array<std::array<float, 4>, 20> vdata{};
    for (unsigned i = 0; i < 16; ++i) {
      vdata[i] = p.default_attributes[i];
      vdata[16 + i / 4][i % 4] = !native_raw || p.vertex_layout[i][3] != 0;
    }
    uniform(2, RHI_STAGE_VERTEX, 3, vdata.data(), sizeof(vdata));
    NV2APSConstants constants{};
    for (unsigned i = 0; i < 8; ++i) {
      auto c0 = argb(r[N::kRasterCombinerFactor0_0 + i]),
           c1 = argb(r[N::kRasterCombinerFactor1_0 + i]);
      std::memcpy(constants.c0[i], c0.data(), 16);
      std::memcpy(constants.c1[i], c1.data(), 16);
    }
    auto fc0 = argb(r[N::kRasterCombinerFinalFactor0]),
         fc1 = argb(r[N::kRasterCombinerFinalFactor1]);
    std::memcpy(constants.final_c0, fc0.data(), 16);
    std::memcpy(constants.final_c1, fc1.data(), 16);
    constants.alpha_ref = float(r[N::kRasterAlphaRef] & 255) / 255;
    constants.alpha_func = (r[N::kRasterAlphaFunc] & 7) + 1;
    constants.alpha_test_enable = enables & 1;
    constants.shadow_func = (r[N::kRasterShadowDepthFunc] & 7) + 1;
    std::array<RhiView *, 4> views{};
    std::array<RhiSampler *, 4> samplers{};
    std::vector<std::unique_ptr<Image>> feedback;
    for (unsigned i = 0; i < 4; ++i) {
      const auto &t = p.textures[i];
      constants.tex_scale[i][0] = t.linear ? 1.f / t.width : 1.f;
      constants.tex_scale[i][1] = t.linear ? 1.f / t.height : 1.f;
      constants.tex_scale[i][2] = constants.tex_scale[i][3] = 1;
      constants.shadow_max[i] = t.format == 0x2e ? 16777215.f : 65535.f;
      if (!t.present)
        continue;
      views[i] = texture(t);
      if (views[i] == color.sampled || (depth && views[i] == depth->sampled)) {
        auto copy = std::make_unique<Image>();
        auto *source = rhi_view_image(views[i]);
        RhiImageDesc desc{};
        rhi_image_get_desc(source, &desc);
        copy->image = rhi_image_create(&desc, nullptr);
        require(copy->image, "feedback snapshot image");
        context_->CopyResource(
            static_cast<ID3D11Resource *>(rhi_d3d11_native_image(copy->image)),
            static_cast<ID3D11Resource *>(rhi_d3d11_native_image(source)));
        copy->sampled = rhi_view_create(copy->image, RHI_VIEW_SAMPLED, nullptr);
        require(copy->sampled, "feedback snapshot view");
        views[i] = copy->sampled;
        feedback.push_back(std::move(copy));
      }
      RhiSamplerDesc sd{};
      unsigned min = (t.filter >> 16) & 255, mag = (t.filter >> 24) & 15;
      require(min <= 7, "texture filter");
      sd.filter = (min == 2 || min == 4 || min == 6 || min == 7 ? 0x10 : 0) |
                  (mag == 1 ? 0 : 4) | (min == 5 || min == 6 ? 1 : 0);
      auto address = [](unsigned m) {
        switch (m) {
        case 0:
        case 1:
          return RHI_ADDRESS_WRAP;
        case 2:
          return RHI_ADDRESS_MIRROR;
        case 3:
        case 5:
          return RHI_ADDRESS_CLAMP;
        case 4:
          return RHI_ADDRESS_BORDER;
        default:
          throw std::runtime_error("phobos D3D11: texture address mode");
        }
      };
      sd.address_u = address(t.address_mode & 15);
      sd.address_v = address((t.address_mode >> 8) & 15);
      sd.address_w = address((t.address_mode >> 16) & 15);
      sd.max_anisotropy = 1;
      sd.compare = RHI_CMP_ALWAYS;
      sd.max_lod = (min <= 2 || min == 7) ? 0.f : float(t.levels - 1);
      auto border = argb(t.border);
      std::memcpy(sd.border, border.data(), 16);
      samplers[i] = cached(samplers_, sd, rhi_sampler_create);
    }
    uniform(3, RHI_STAGE_PIXEL, 0, &constants, sizeof(constants));
    rhi_set_textures(0, 4, views.data());
    rhi_set_samplers(0, 4, samplers.data());
    std::vector<Nv2aHleDraw::Vertex> decoded;
    const auto *vertex_data = &p.vertices;
    if (p.raw_vertex_count && !native_raw) {
      decoded.resize(p.raw_vertex_count);
      for (unsigned n = 0; n < p.raw_vertex_count; ++n)
        for (unsigned a = 0; a < 16; ++a) {
          const auto &layout = p.vertex_layout[a];
          auto &v = decoded[n][a];
          v = p.default_attributes[a];
          if (!layout[3])
            continue;
          require(layout[0] >= 0 && layout[1] >= 0 && layout[3] > 0 &&
                      layout[3] <= 4,
                  "vertex layout");
          unsigned type = layout[2], bpp = type == 2 || type == 6   ? 4
                                           : type == 1 || type == 5 ? 2
                                                                    : 1;
          size_t offset = size_t(layout[0]) + size_t(layout[1]) * n;
          require(type == 0 || type == 1 || type == 2 || type == 4 ||
                      type == 5 || type == 6,
                  "vertex format");
          require(offset + (type == 6 ? 4 : bpp * layout[3]) <=
                      p.raw_vertices.size(),
                  "vertex source bounds");
          v = {0, 0, 0, 1};
          if (type == 6) {
            uint32_t bits;
            std::memcpy(&bits, p.raw_vertices.data() + offset, 4);
            v[0] = std::max(float(int32_t(bits << 21) >> 21) / 1023.f, -1.f);
            v[1] = std::max(float(int32_t(bits << 10) >> 21) / 1023.f, -1.f);
            v[2] = std::max(float(int32_t(bits) >> 22) / 511.f, -1.f);
          } else
            for (int k = 0; k < layout[3]; ++k) {
              const auto *source = p.raw_vertices.data() + offset + k * bpp;
              if (type == 2)
                std::memcpy(&v[k], source, 4);
              else if (bpp == 1)
                v[k] = float(*source) / 255;
              else {
                int16_t value;
                std::memcpy(&value, source, 2);
                v[k] = type == 1 ? std::max(float(value) / 32767, -1.f)
                                 : float(value);
              }
            }
        }
      vertex_data = &decoded;
    }
    const size_t vertex_count =
        native_raw ? p.raw_vertex_count : vertex_data->size();
    require(vertex_count, "missing vertices");
    for (auto index : p.indices)
      require(index < vertex_count, "index bounds");
    std::vector<uint32_t> converted;
    const auto *index_data = &p.indices;
    uint32_t topology = RHI_TOPOLOGY_TRIANGLES;
    switch (p.primitive) {
    case 1:
      topology = RHI_TOPOLOGY_POINTS;
      break;
    case 2:
      topology = RHI_TOPOLOGY_LINES;
      break;
    case 3:
      converted = p.indices;
      converted.push_back(p.indices.front());
      index_data = &converted;
      topology = RHI_TOPOLOGY_LINE_STRIP;
      break;
    case 4:
      topology = RHI_TOPOLOGY_LINE_STRIP;
      break;
    case 5:
      break;
    case 6:
      topology = RHI_TOPOLOGY_TRIANGLE_STRIP;
      break;
    case 7:
    case 10:
      for (size_t i = 2; i < p.indices.size(); ++i)
        converted.insert(converted.end(),
                         {p.indices[0], p.indices[i - 1], p.indices[i]});
      index_data = &converted;
      break;
    case 8:
    case 9:
      for (size_t i = 0; i + 3 < p.indices.size();
           i += p.primitive == 8 ? 4 : 2) {
        auto a = p.indices[i], b = p.indices[i + 1], c = p.indices[i + 2],
             d = p.indices[i + 3];
        if (p.primitive == 8)
          converted.insert(converted.end(), {a, b, c, a, c, d});
        else
          converted.insert(converted.end(), {a, b, d, a, d, c});
      }
      index_data = &converted;
      break;
    default:
      throw std::runtime_error("phobos D3D11: primitive mode");
    }
    if (index_data->empty())
      return true;
    if (native_raw) {
      stream(vertices_, vertex_capacity_, RHI_BIND_VERTEX,
             p.raw_vertices.data(), p.raw_vertices.size());
      std::string key(reinterpret_cast<const char *>(formats.data()),
                      sizeof(formats));
      auto &layout = prog.raw_layouts[key];
      if (!layout) {
        std::vector<RhiVertexElement> elements;
        for (unsigned i = 0; i < 16; ++i)
          elements.push_back(
              {"ATTR", i,
               formats[i] ? formats[i] : RHI_FORMAT_R32G32B32A32_FLOAT, i, 0});
        layout = rhi_vertex_layout_create(elements.data(),
                                          uint32_t(elements.size()), prog.vs);
        require(layout, "raw GPU vertex layout");
      }
      rhi_set_vertex_layout(layout);
      for (unsigned i = 0; i < 16; ++i)
        rhi_set_vertex_buffer(i, formats[i] ? vertices_ : nullptr,
                              p.vertex_layout[i][1], p.vertex_layout[i][0]);
      ++raw_draws_;
    } else {
      stream(vertices_, vertex_capacity_, RHI_BIND_VERTEX, vertex_data->data(),
             vertex_data->size() * sizeof(Nv2aHleDraw::Vertex));
      rhi_set_vertex_buffer(0, vertices_, sizeof(Nv2aHleDraw::Vertex), 0);
      ++expanded_draws_;
    }
    stream(indices_, index_capacity_, RHI_BIND_INDEX, index_data->data(),
           index_data->size() * 4);
    rhi_set_index_buffer(indices_, 32, 0);
    rhi_set_topology(topology);
    rhi_draw_indexed(uint32_t(index_data->size()), 0, 0);
    require(SUCCEEDED(device_->GetDeviceRemovedReason()), "GPU draw failure");
    if (mask && right > x && bottom > y)
      color.dirty = true;
    if (depth && right > x && bottom > y &&
        (((enables & 8) && r[N::kRasterDepthMask]) || (enables & 32)))
      depth->dirty = true;
    ++draws_;
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
}
} // namespace
std::unique_ptr<Nv2aRenderer> create_nv2a_hle_renderer(std::string &error) {
  try {
    auto r = std::make_unique<PhobosRenderer>();
    if (!r->initialize(error))
      return {};
    return r;
  } catch (const std::exception &e) {
    error = e.what();
    return {};
  }
}
} // namespace xlle
