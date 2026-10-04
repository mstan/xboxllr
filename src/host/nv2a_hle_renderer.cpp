// Separate native graphics implementation. xemu translates vertex programs and
// register combiners; NoRain supplies the texture layout algorithms. This class
// never invokes the LLE renderer or submits software-transformed triangles.
#include "host/nv2a_hle_renderer.hpp"
#include "machine/nv2a.hpp"
#include "machine/nv2a_hle_draw.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>
extern "C" {
#include "third_party/norain/d3d_texture_model.h"
#include "third_party/xemu/psh.h"
#include "third_party/xemu/swizzle.h"
#include "third_party/xemu/vsh-prog.h"
MString *xlle_hle_gen_psh(const PshState *, GenPshGlslOptions);
}

namespace xlle {
namespace {
using N = Nv2a;
float as_float(uint32_t value) {
  float f;
  std::memcpy(&f, &value, 4);
  return f;
}
bool overlap(uint32_t a, size_t an, uint32_t b, size_t bn) {
  return uint64_t(a) < uint64_t(b) + bn && uint64_t(b) < uint64_t(a) + an;
}
std::array<float, 4> argb(uint32_t c) {
  return {float((c >> 16) & 255) / 255, float((c >> 8) & 255) / 255,
          float(c & 255) / 255, float(c >> 24) / 255};
}
#define HLE_GL_PROCS(X)                                                                \
  X(PFNGLCREATESHADERPROC, CreateShader)                                               \
  X(PFNGLSHADERSOURCEPROC, ShaderSource)                                               \
  X(PFNGLCOMPILESHADERPROC, CompileShader)                                             \
  X(PFNGLGETSHADERIVPROC, GetShaderiv) X(                                              \
      PFNGLGETSHADERINFOLOGPROC,                                                       \
      GetShaderInfoLog) X(PFNGLDELETESHADERPROC,                                       \
                          DeleteShader) X(PFNGLCREATEPROGRAMPROC,                      \
                                          CreateProgram) X(PFNGLATTACHSHADERPROC,      \
                                                           AttachShader)               \
      X(PFNGLLINKPROGRAMPROC, LinkProgram) X(                                          \
          PFNGLGETPROGRAMIVPROC,                                                       \
          GetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC,                                  \
                          GetProgramInfoLog) X(PFNGLDELETEPROGRAMPROC,                 \
                                               DeleteProgram) X(PFNGLUSEPROGRAMPROC,   \
                                                                UseProgram)            \
          X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) X(                        \
              PFNGLUNIFORM1IPROC,                                                      \
              Uniform1i) X(PFNGLUNIFORM1FPROC,                                         \
                           Uniform1f) X(PFNGLUNIFORM2FPROC,                            \
                                        Uniform2f) X(PFNGLUNIFORM2IPROC,               \
                                                     Uniform2i)                        \
              X(PFNGLUNIFORM4FVPROC, Uniform4fv) X(                                    \
                  PFNGLUNIFORM4IVPROC,                                                 \
                  Uniform4iv) X(PFNGLGENBUFFERSPROC,                                   \
                                GenBuffers) X(PFNGLBINDBUFFERPROC,                     \
                                              BindBuffer) X(PFNGLBUFFERDATAPROC,       \
                                                            BufferData)                \
                  X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) X(                          \
                      PFNGLBINDBUFFERBASEPROC,                                         \
                      BindBufferBase) X(PFNGLGENVERTEXARRAYSPROC,                      \
                                        GenVertexArrays)                               \
                      X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) X(                  \
                          PFNGLDELETEVERTEXARRAYSPROC,                                 \
                          DeleteVertexArrays) X(PFNGLGENFRAMEBUFFERSPROC,              \
                                                GenFramebuffers)                       \
                          X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) X(              \
                              PFNGLFRAMEBUFFERTEXTURE2DPROC,                           \
                              FramebufferTexture2D) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, \
                                                      CheckFramebufferStatus)          \
                              X(PFNGLDELETEFRAMEBUFFERSPROC,                           \
                                DeleteFramebuffers) X(PFNGLACTIVETEXTUREPROC,          \
                                                      ActiveTexture)                   \
                                  X(PFNGLBLENDEQUATIONPROC, BlendEquation) X(          \
                                      PFNGLBLENDCOLORPROC,                             \
                                      BlendColor) X(PFNGLCOMPRESSEDTEXIMAGE2DPROC,     \
                                                    CompressedTexImage2D)              \
                                      X(PFNGLCOPYIMAGESUBDATAPROC,                     \
                                        CopyImageSubData) X(PFNGLTEXIMAGE3DPROC,       \
                                                            TexImage3D)                \
                                          X(PFNGLGENSAMPLERSPROC, GenSamplers) X(      \
                                              PFNGLDELETESAMPLERSPROC,                 \
                                              DeleteSamplers)                          \
                                              X(PFNGLBINDSAMPLERPROC,                  \
                                                BindSampler)                           \
                                                  X(PFNGLSAMPLERPARAMETERIPROC,        \
                                                    SamplerParameteri)                 \
                                                      X(PFNGLSAMPLERPARAMETERFVPROC,   \
                                                        SamplerParameterfv)

class HleRenderer final : public Nv2aRenderer {
  SDL_Window *window_ = nullptr;
  SDL_GLContext context_ = nullptr;
  bool owns_video_ = false;
#define DECL(type, name) type name = nullptr;
  HLE_GL_PROCS(DECL)
#undef DECL
  GLuint vao_ = 0, vbo_ = 0, ibo_ = 0, fbo_ = 0;
  GLuint samplers_[4]{};
  struct Plane {
    Nv2aHleSurface desc;
    GLuint texture = 0;
    bool valid = false, dirty = false;
  };
  std::vector<Plane> planes_;
  struct Texture {
    Nv2aHleTexture desc;
    GLuint id = 0;
  };
  std::map<std::string, Texture> textures_;
  struct Program {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> uniforms;
  };
  std::unordered_map<std::string, Program> programs_;
  uint64_t draws_ = 0, vertices_ = 0, uploads_ = 0, resolves_ = 0,
           target_samples_ = 0;
  struct Context {
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    HleRenderer &r;
    explicit Context(HleRenderer &owner) : r(owner) {
      if (previous_context != r.context_ &&
          !SDL_GL_MakeCurrent(r.window_, r.context_))
        throw std::runtime_error(SDL_GetError());
    }
    ~Context() {
      // Keep our context current while this thread has no competing GL owner.
      // Detaching after every draw forces unnecessary driver transitions.
      if (previous_context && previous_context != r.context_)
        SDL_GL_MakeCurrent(previous_window, previous_context);
    }
  };
  struct SurfaceGl {
    unsigned bpp;
    GLint internal;
    GLenum format, type;
  };
  static SurfaceGl surface_gl(const Nv2aHleSurface &s) {
    if (s.depth)
      return ((s.format >> 4) & 15) == 1
                 ? SurfaceGl{2, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT,
                             GL_UNSIGNED_SHORT}
                 : SurfaceGl{4, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL,
                             GL_UNSIGNED_INT_24_8};
    switch (s.format & 15) {
    case 1:
    case 2:
      return {2, GL_RGB5, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV};
    case 3:
      return {2, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5};
    case 4:
    case 5:
      return {4, GL_RGB8, GL_BGRA, GL_UNSIGNED_BYTE};
    case 8:
      return {4, GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE};
    default:
      throw std::runtime_error("unsupported color surface format " +
                               std::to_string(s.format & 15));
    }
  }
  static size_t plane_size(const Plane &p) {
    return size_t(p.desc.pitch) * p.desc.height;
  }
  Plane *plane(uint32_t address, uint32_t pitch, uint32_t height, bool depth) {
    for (auto &p : planes_)
      if (p.valid && p.desc.address == address && p.desc.pitch == pitch &&
          p.desc.height == height && p.desc.depth == depth)
        return &p;
    return nullptr;
  }
  GLint uniform(Program &p, const std::string &name) {
    auto found = p.uniforms.find(name);
    if (found != p.uniforms.end())
      return found->second;
    return p.uniforms.emplace(name, GetUniformLocation(p.id, name.c_str()))
        .first->second;
  }
  GLuint compile(GLenum type, const std::string &source) {
    GLuint id = CreateShader(type);
    const char *ptr = source.c_str();
    ShaderSource(id, 1, &ptr, nullptr);
    CompileShader(id);
    GLint ok = 0;
    GetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[8192]{};
      GetShaderInfoLog(id, sizeof(log), nullptr, log);
      DeleteShader(id);
      throw std::runtime_error(std::string(type == GL_VERTEX_SHADER
                                               ? "vertex shader: "
                                               : "pixel shader: ") +
                               log);
    }
    return id;
  }
  std::string vertex_source(const Nv2aHleDraw &p) {
    std::string header = R"GLSL(#version 430 core
layout(std430,binding=0) readonly buffer VertexInputs { vec4 attributes[]; };
uniform vec4 c[192];
uniform vec2 surfaceSize;
uniform vec4 clipRange;
uniform vec4 ambientColor;
uniform int ambientLighting, specularAlphaFromVertex;
out vec4 vtxD0, vtxD1, vtxB0, vtxB1;
out float vtxFog;
out vec4 vtxT0, vtxT1, vtxT2, vtxT3;
flat out vec4 vtxPos0, vtxPos1, vtxPos2;
flat out float triMZ;
noperspective out float hleDepth;
vec4 oPos=vec4(0,0,0,1), oD0=vec4(0,0,0,1), oD1=vec4(0,0,0,1);
vec4 oB0=vec4(0,0,0,1), oB1=vec4(0,0,0,1), oFog=vec4(0,0,0,1);
vec4 oPts=vec4(0,0,0,1), oT0=vec4(0,0,0,1), oT1=vec4(0,0,0,1);
vec4 oT2=vec4(0,0,0,1), oT3=vec4(0,0,0,1);
float clampAwayZeroInf(float t) {
    return t >= 0.0 ? clamp(t, uintBitsToFloat(0x1f800000u), uintBitsToFloat(0x5f800000u)) :
                      clamp(t, uintBitsToFloat(0xdf800000u), uintBitsToFloat(0x9f800000u));
}
vec2 roundScreenCoords(vec2 p) { return trunc(p*16.0)/16.0; }
vec4 NaNToOne(vec4 c) { return mix(c,vec4(1),isnan(c)); }
vec4 color(vec4 c) { return clamp(NaNToOne(c),0.0,1.0); }
)GLSL";
    for (unsigned i = 0; i < 16; ++i)
      header += "#define v" + std::to_string(i) +
                " attributes[gl_VertexID*16+" + std::to_string(i) + "]\n";
    std::string body;
    if (p.transform_mode == 2) {
      MString *h = mstring_new();
      MString *b = mstring_new();
      pgraph_glsl_gen_vsh_prog(0x2078, p.program.front().data(),
                               unsigned(p.program.size()), h, b);
      header += mstring_get_str(h);
      body = mstring_get_str(b);
      mstring_unref(h);
      mstring_unref(b);
    } else {
      body = R"GLSL(
oPos = vec4(dot(v0,c[0]),dot(v0,c[1]),dot(v0,c[2]),dot(v0,c[3]));
oPos.w = clampAwayZeroInf(oPos.w);
oPos.xyz /= oPos.w;
oPos.xy = roundScreenCoords(oPos.xy + c[59].xy);
vec4 vtxPos = oPos;
oPos.xy = (2.0*oPos.xy-surfaceSize)/surfaceSize;
oPos.xyz *= oPos.w;
oD0=ambientLighting!=0?ambientColor:v3; oD1=vec4(0,0,0,(ambientLighting!=0 && specularAlphaFromVertex!=0)?v4.w:1.0); oT0=v9; oT1=v10; oT2=v11; oT3=v12;
)GLSL";
    }
    return header + "\nvoid main() {\n" + body + R"GLSL(
gl_Position = vec4(oPos.xy, 0.0, oPos.w);
hleDepth = vtxPos.z;
vtxD0=color(oD0); vtxD1=color(oD1); vtxB0=color(oB0); vtxB1=color(oB1);
vtxFog=oFog.x; vtxT0=oT0; vtxT1=oT1; vtxT2=oT2; vtxT3=oT3;
vtxPos0=vtxPos; vtxPos1=vtxPos; vtxPos2=vtxPos; triMZ=0.0;
gl_PointSize=max(oPts.x,1.0);
}
)GLSL";
  }
  Program &program(const Nv2aHleDraw &p) {
    if (p.state.size() != N::kRasterStateWords)
      throw std::runtime_error("invalid HLE state size");
    if (p.transform_mode == 2) {
      if (p.program.empty() || !(p.program.back()[3] & 1))
        throw std::runtime_error("unterminated vertex shader");
      for (const auto &op : p.program)
        if (((op[1] >> 21) & 15) > 13 || ((op[3] & 0xf000) && !(op[3] & 0x800)))
          throw std::runtime_error(
              "unsupported vertex instruction or persistent constant write");
    } else if (p.transform_mode != 0)
      throw std::runtime_error("unsupported vertex mode");
    const auto &r = p.state;
    PshState s{};
    s.combiner_control = r[N::kRasterCombinerControl];
    s.shader_stage_program = r[N::kRasterShaderStageProgram];
    s.other_stage_input = r[N::kRasterShaderOtherStageInput];
    s.final_inputs_0 = r[N::kRasterCombinerSpecularFog0];
    s.final_inputs_1 = r[N::kRasterCombinerSpecularFog1];
    for (unsigned i = 0; i < 8; ++i) {
      s.rgb_inputs[i] = r[N::kRasterCombinerColorIcw0 + i];
      s.rgb_outputs[i] = r[N::kRasterCombinerColorOcw0 + i];
      s.alpha_inputs[i] = r[N::kRasterCombinerAlphaIcw0 + i];
      s.alpha_outputs[i] = r[N::kRasterCombinerAlphaOcw0 + i];
    }
    for (unsigned i = 0; i < 4; ++i) {
      s.dim_tex[i] = p.textures[i].dimensions;
      s.rect_tex[i] = p.textures[i].linear;
      s.tex_cubemap[i] = p.textures[i].cubemap;
      s.shadow_map[i] = p.textures[i].format == 0x30 ||
                        p.textures[i].format == 0x2c ||
                        p.textures[i].format == 0x2e;
      s.tex_x8y24[i] = p.textures[i].format == 0x2e;
      s.alphakill[i] = (r[N::kRasterTextureControl0_0 + i] & 4) != 0;
    }
    s.shadow_depth_func =
        static_cast<PshShadowDepthFunc>(r[N::kRasterShadowDepthFunc] & 7);
    s.smooth_shading = true;
    s.depth_clipping = true;
    s.depth_format =
        ((p.color.format >> 4) & 15) == 1 ? DEPTH_FORMAT_D16 : DEPTH_FORMAT_D24;
    s.alpha_test = (r[N::kRasterEnables] & 1) != 0;
    s.alpha_func = static_cast<PshAlphaFunc>(r[N::kRasterAlphaFunc] & 7);
    std::string key(reinterpret_cast<const char *>(&s), sizeof(s));
    key.append(reinterpret_cast<const char *>(&p.transform_mode), 4);
    if (!p.program.empty())
      key.append(reinterpret_cast<const char *>(p.program.data()),
                 p.program.size() * 16);
    auto found = programs_.find(key);
    if (found != programs_.end())
      return found->second;
    MString *source = xlle_hle_gen_psh(&s, {false, 0, 0});
    if (!source)
      throw std::runtime_error(
          "unsupported pixel shader state, texture program=" +
          std::to_string(s.shader_stage_program));
    std::string fs = mstring_get_str(source);
    mstring_unref(source);
    const auto begin =
        fs.find("vec2 unscaled_xy = gl_FragCoord.xy / surfaceScale;");
    const std::string end_marker = "zvalue += depthFactor*triMZ;\n";
    const auto end = fs.find(end_marker, begin);
    if (begin == std::string::npos || end == std::string::npos)
      throw std::runtime_error("donor depth adapter mismatch");
    fs.replace(begin, end + end_marker.size() - begin,
               "float "
               "zvalue=hleDepth+depthOffset+max(abs(dFdx(hleDepth)),abs(dFdy("
               "hleDepth)))*depthFactor;\n");
    fs.insert(fs.find("void main() {"), "noperspective in float hleDepth;\n");
    GLuint vs = compile(GL_VERTEX_SHADER, vertex_source(p)), ps = 0, id = 0;
    try {
      ps = compile(GL_FRAGMENT_SHADER, fs);
      id = CreateProgram();
      AttachShader(id, vs);
      AttachShader(id, ps);
      LinkProgram(id);
      GLint ok = 0;
      GetProgramiv(id, GL_LINK_STATUS, &ok);
      if (!ok) {
        char log[8192]{};
        GetProgramInfoLog(id, sizeof(log), nullptr, log);
        throw std::runtime_error(log);
      }
    } catch (...) {
      DeleteShader(vs);
      if (ps)
        DeleteShader(ps);
      if (id)
        DeleteProgram(id);
      throw;
    }
    DeleteShader(vs);
    DeleteShader(ps);
    return programs_.emplace(std::move(key), Program{id, {}}).first->second;
  }
  Plane &ensure_plane(const Nv2aHleSurface &s) {
    if (auto *existing = plane(s.address, s.pitch, s.height, s.depth)) {
      if (existing->desc.format == s.format)
        return *existing;
      if (existing->dirty)
        throw std::runtime_error("dirty target format alias requires resolve");
      existing->valid = false;
    }
    if (s.bytes.size() != size_t(s.pitch) * s.height)
      throw std::runtime_error("missing target contents");
    // A new view replaces every overlapping view after the device has
    // resolved its contents. Otherwise a later clear/readback can revive
    // an obsolete allocation and overwrite the guest's current owner.
    for (auto &alias : planes_) {
      if (!alias.valid || !overlap(s.address, s.bytes.size(),
                                   alias.desc.address, plane_size(alias)))
        continue;
      if (alias.dirty)
        throw std::runtime_error("dirty overlapping target requires resolve");
      alias.valid = false;
    }
    Plane *out = nullptr;
    for (auto &p : planes_)
      if (!p.valid) {
        out = &p;
        break;
      }
    if (!out) {
      planes_.emplace_back();
      out = &planes_.back();
    }
    if (!out->texture)
      glGenTextures(1, &out->texture);
    out->desc = s;
    out->desc.bytes.clear();
    out->valid = true;
    out->dirty = false;
    std::vector<uint8_t> linear;
    const void *pixels = s.bytes.data();
    const auto format = surface_gl(s);
    if (s.swizzled) {
      linear.resize(s.bytes.size());
      if (!recomp_d3d_texture_unswizzle(s.bytes.data(), linear.data(), s.width,
                                        s.height, format.bpp))
        throw std::runtime_error("target unswizzle");
      pixels = linear.data();
    }
    glBindTexture(GL_TEXTURE_2D, out->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, format.internal, s.width, s.height, 0,
                 format.format, format.type, pixels);
    ++uploads_;
    return *out;
  }
  static unsigned bytes_per_texel(unsigned fmt) {
    switch (fmt) {
    case 0:
    case 0x19:
    case 0x0b:
      return 1;
    case 2:
    case 3:
    case 4:
    case 5:
    case 0x10:
    case 0x11:
    case 0x30:
    case 0x2c:
    case 0x35:
      return 2;
    case 6:
    case 7:
    case 0x12:
    case 0x1e:
    case 0x2e:
      return 4;
    default:
      throw std::runtime_error("unsupported texture format " +
                               std::to_string(fmt));
    }
  }
  GLuint texture(const Nv2aHleTexture &t) {
    if (t.resident) {
      auto *s = plane(t.address, t.pitch, t.height,
                      t.format == 0x30 || t.format == 0x2c || t.format == 0x2e);
      if (!s)
        throw std::runtime_error("resident texture lost");
      ++target_samples_;
      return s->texture;
    }
    // Exact source ownership/comparison, cache only upload/layout work.
    std::string key =
        std::to_string(t.address) + ":" + std::to_string(t.format) + ":" +
        std::to_string(t.width) + ":" + std::to_string(t.height) + ":" +
        std::to_string(t.levels) + ":" + std::to_string(t.pitch) + ":" +
        std::to_string(t.cubemap) + ":" + std::to_string(t.depth);
    auto &cached = textures_[key];
    if (cached.id && cached.desc.bytes == t.bytes &&
        cached.desc.palette == t.palette)
      return cached.id;
    if (!cached.id)
      glGenTextures(1, &cached.id);
    cached.desc = t;
    const GLenum target =
        t.cubemap ? GL_TEXTURE_CUBE_MAP
                  : (t.dimensions == 3 ? GL_TEXTURE_3D : GL_TEXTURE_2D);
    glBindTexture(target, cached.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const bool bc = t.format == 0x0c || t.format == 0x0e || t.format == 0x0f;
    size_t off = 0;
    for (unsigned face = 0; face < (t.cubemap ? 6u : 1u); ++face) {
      unsigned w = t.width, h = t.height, d = t.depth;
      for (unsigned level = 0; level < t.levels; ++level) {
        GLenum dest =
            t.cubemap ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : target;
        if (bc) {
          if (d != 1)
            throw std::runtime_error("compressed volume texture unsupported");
          size_t bytes =
              size_t((w + 3) / 4) * ((h + 3) / 4) * (t.format == 0x0c ? 8 : 16);
          if (off + bytes > t.bytes.size())
            throw std::runtime_error("compressed mip span");
          CompressedTexImage2D(
              dest, level,
              t.format == 0x0c   ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
              : t.format == 0x0e ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
                                 : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
              w, h, 0, GLsizei(bytes), t.bytes.data() + off);
          off += bytes;
        } else {
          const unsigned bpp = bytes_per_texel(t.format);
          const size_t bytes =
              t.linear ? size_t(t.pitch) * h * d : size_t(w) * h * d * bpp;
          if (off + bytes > t.bytes.size())
            throw std::runtime_error("texture mip span");
          std::vector<uint8_t> linear(size_t(w) * h * d * bpp),
              rgba(size_t(w) * h * d * 4);
          if (t.linear) {
            for (unsigned y = 0; y < h; ++y)
              std::memcpy(linear.data() + size_t(y) * w * bpp,
                          t.bytes.data() + off + size_t(y) * t.pitch,
                          size_t(w) * bpp);
          } else if (d > 1)
            unswizzle_box(t.bytes.data() + off, w, h, d, linear.data(), w * bpp,
                          w * h * bpp, bpp);
          else if (!recomp_d3d_texture_unswizzle(t.bytes.data() + off,
                                                 linear.data(), w, h, bpp))
            throw std::runtime_error("texture unswizzle");
          if (t.format == 0x30 || t.format == 0x2c || t.format == 0x2e ||
              t.format == 0x35) {
            bool z24 = t.format == 0x2e;
            glTexImage2D(
                dest, level,
                z24 ? GL_DEPTH24_STENCIL8
                    : (t.format == 0x35 ? GL_R16 : GL_DEPTH_COMPONENT16),
                w, h, 0,
                z24 ? GL_DEPTH_STENCIL
                    : (t.format == 0x35 ? GL_RED : GL_DEPTH_COMPONENT),
                z24 ? GL_UNSIGNED_INT_24_8 : GL_UNSIGNED_SHORT, linear.data());
            GLint swiz[4]{GL_RED, z24 ? GL_ONE : GL_ZERO, GL_ZERO, GL_ZERO};
            if (t.format == 0x35) {
              swiz[1] = swiz[2] = GL_RED;
              swiz[3] = GL_ONE;
            }
            glTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, swiz);
          } else {
            for (size_t n = 0; n < size_t(w) * h * d; ++n) {
              uint32_t c = 0;
              std::memcpy(&c, linear.data() + n * bpp, bpp);
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
              case 0x0b:
                if (size_t(c) * 4 + 4 > t.palette.size())
                  throw std::runtime_error("palette index");
                std::memcpy(&c, t.palette.data() + c * 4, 4);
                break;
              case 5:
              case 0x11: {
                unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
                c = 0xff000000 | ((r * 255 / 31) << 16) |
                    ((g * 255 / 63) << 8) | (b * 255 / 31);
                break;
              }
              case 2:
              case 0x10: {
                unsigned a = c >> 15, r = (c >> 10) & 31, g = (c >> 5) & 31,
                         b = c & 31;
                c = (a ? 0xff000000 : 0) | ((r * 255 / 31) << 16) |
                    ((g * 255 / 31) << 8) | (b * 255 / 31);
                break;
              }
              case 4: {
                unsigned a = c >> 12, r = (c >> 8) & 15, g = (c >> 4) & 15,
                         b = c & 15;
                c = (a * 17 << 24) | (r * 17 << 16) | (g * 17 << 8) | (b * 17);
                break;
              }
              case 3: {
                unsigned r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
                c = 0xff000000 | ((r * 255 / 31) << 16) |
                    ((g * 255 / 31) << 8) | (b * 255 / 31);
                break;
              }
              default:
                throw std::runtime_error("texture conversion");
              }
              std::memcpy(rgba.data() + n * 4, &c, 4);
            }
            if (t.dimensions == 3)
              TexImage3D(dest, level, GL_RGBA8, w, h, d, 0, GL_BGRA,
                         GL_UNSIGNED_BYTE, rgba.data());
            else
              glTexImage2D(dest, level, GL_RGBA8, w, h, 0, GL_BGRA,
                           GL_UNSIGNED_BYTE, rgba.data());
          }
          off += bytes;
        }
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        d = std::max(1u, d / 2);
      }
      if (t.cubemap)
        off = (off + 127) & ~size_t(127);
    }
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, t.levels - 1);
    ++uploads_;
    return cached.id;
  }
  static GLint wrap(unsigned mode) {
    switch (mode) {
    case 0:
    case 1:
      return GL_REPEAT;
    case 2:
      return GL_MIRRORED_REPEAT;
    case 3:
      return GL_CLAMP_TO_EDGE;
    case 4:
      return GL_CLAMP_TO_BORDER;
    case 5:
      return GL_CLAMP_TO_EDGE;
    default:
      throw std::runtime_error("texture wrap");
    }
  }
  void read_plane(Plane &p, std::vector<uint8_t> &result) {
    const auto format = surface_gl(p.desc);
    result.resize(plane_size(p));
    glBindTexture(GL_TEXTURE_2D, p.texture);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, format.format, format.type, result.data());
    if (p.desc.swizzled) {
      auto linear = result;
      uint32_t mx, my;
      recomp_d3d_texture_swizzle_masks(p.desc.width, p.desc.height, &mx, &my);
      unsigned bpp = format.bpp;
      for (unsigned y = 0; y < p.desc.height; ++y)
        for (unsigned x = 0; x < p.desc.width; ++x)
          std::memcpy(
              result.data() +
                  size_t(recomp_d3d_texture_swizzle_offset(x, y, mx, my)) * bpp,
              linear.data() + (size_t(y) * p.desc.width + x) * bpp, bpp);
    }
    p.dirty = false;
    ++resolves_;
  }

public:
  ~HleRenderer() override {
    if (context_) {
      SDL_GL_MakeCurrent(window_, context_);
      for (auto &p : planes_)
        glDeleteTextures(1, &p.texture);
      for (auto &t : textures_)
        glDeleteTextures(1, &t.second.id);
      for (auto &p : programs_)
        DeleteProgram(p.second.id);
      if (DeleteSamplers)
        DeleteSamplers(4, samplers_);
      if (DeleteBuffers) {
        DeleteBuffers(1, &vbo_);
        DeleteBuffers(1, &ibo_);
      }
      if (DeleteVertexArrays)
        DeleteVertexArrays(1, &vao_);
      if (DeleteFramebuffers)
        DeleteFramebuffers(1, &fbo_);
      SDL_GL_DestroyContext(context_);
    }
    if (window_)
      SDL_DestroyWindow(window_);
    if (owns_video_)
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
    std::printf("[hle-renderer] draws=%llu vertices=%llu shaders=%zu "
                "uploads=%llu resolves=%llu resident-texture-samples=%llu\n",
                (unsigned long long)draws_, (unsigned long long)vertices_,
                programs_.size(), (unsigned long long)uploads_,
                (unsigned long long)resolves_,
                (unsigned long long)target_samples_);
  }
  bool initialize(std::string &error) {
    owns_video_ = !(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO);
    if (owns_video_ && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
      error = SDL_GetError();
      return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    auto *old_window = SDL_GL_GetCurrentWindow();
    auto old_context = SDL_GL_GetCurrentContext();
    window_ = SDL_CreateWindow("xboxlle HLE GPU", 64, 64,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window_) {
      error = SDL_GetError();
      return false;
    }
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
      error = SDL_GetError();
      return false;
    }
    SDL_GL_MakeCurrent(window_, context_);
#define LOAD(type, name)                                                       \
  name = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name));            \
  if (!name) {                                                                 \
    error = "Missing gl" #name;                                                \
    return false;                                                              \
  }
    HLE_GL_PROCS(LOAD)
#undef LOAD
    GenVertexArrays(1, &vao_);
    GenBuffers(1, &vbo_);
    GenBuffers(1, &ibo_);
    GenFramebuffers(1, &fbo_);
    GenSamplers(4, samplers_);
    glDisable(GL_DITHER);
    std::printf("[hle-renderer] device=%s vertex=xemu pixel=xemu "
                "textures=NoRain native-BC\n",
                glGetString(GL_RENDERER));
    SDL_GL_MakeCurrent(old_window, old_context);
    return true;
  }
  const char *name() const override { return "hle-native"; }
  bool
  supports_generic_combiner(const Nv2aGenericCombinerPacket &) const override {
    return false;
  }
  bool submit_generic_combiner(const Nv2aGenericCombinerPacket &,
                               Nv2aGenericCombinerResult &,
                               std::string &e) override {
    e = "LLE packet submitted to HLE renderer";
    return false;
  }
  bool requires_resident_target_switch_resolve() const override {
    return false;
  }
  bool has_resident_color_surface(uint32_t a, size_t n, uint32_t pitch,
                                  uint32_t h, uint32_t fmt) const override {
    for (auto &p : planes_)
      if (p.valid && !p.desc.depth && p.desc.address == a &&
          plane_size(p) == n && p.desc.pitch == pitch && p.desc.height == h &&
          (!fmt || p.desc.format == fmt))
        return true;
    return false;
  }
  bool has_resident_depth_surface(uint32_t a, size_t n, uint32_t pitch,
                                  uint32_t h, uint32_t fmt) const override {
    for (auto &p : planes_)
      if (p.valid && p.desc.depth && p.desc.address == a &&
          plane_size(p) == n && p.desc.pitch == pitch && p.desc.height == h &&
          (!fmt || p.desc.format == fmt))
        return true;
    return false;
  }
  bool resolve_resident_color_surface(uint32_t a, size_t n,
                                      Nv2aResidentColorResolve &out,
                                      std::string &error) override {
    out = {};
    try {
      Context context(*this);
      for (auto &p : planes_)
        if (p.valid && p.dirty && !p.desc.depth &&
            overlap(a, n, p.desc.address, plane_size(p))) {
          out.address = p.desc.address;
          read_plane(p, out.data);
          break;
        }
      return true;
    } catch (const std::exception &e) {
      error = e.what();
      return false;
    }
  }
  bool resolve_resident_depth_surface(uint32_t a, size_t n,
                                      Nv2aResidentDepthResolve &out,
                                      std::string &error) override {
    out = {};
    try {
      Context context(*this);
      for (auto &p : planes_)
        if (p.valid && p.dirty && p.desc.depth &&
            overlap(a, n, p.desc.address, plane_size(p))) {
          out.address = p.desc.address;
          read_plane(p, out.data);
          break;
        }
      return true;
    } catch (const std::exception &e) {
      error = e.what();
      return false;
    }
  }
  void invalidate_resident_color_surface(uint32_t a, size_t n,
                                         uint32_t) override {
    for (auto &p : planes_)
      if (p.valid && !p.desc.depth &&
          overlap(a, n, p.desc.address, plane_size(p)))
        p.valid = false;
  }
  void invalidate_resident_depth_surface(uint32_t a, size_t n,
                                         uint32_t) override {
    for (auto &p : planes_)
      if (p.valid && p.desc.depth &&
          overlap(a, n, p.desc.address, plane_size(p)))
        p.valid = false;
  }
  void kick_pending_gpu_work() override {
    Context context(*this);
    glFlush();
  }
  bool clear_resident_surface(uint32_t ca, size_t cn, bool cc, uint32_t cv,
                              uint32_t da, size_t dn, bool cd, uint32_t dv,
                              bool &fill, std::string &error) override {
    Plane *c = nullptr;
    Plane *d = nullptr;
    for (auto &p : planes_)
      if (p.valid) {
        if (!p.desc.depth && p.desc.address == ca && plane_size(p) == cn)
          c = &p;
        if (p.desc.depth && p.desc.address == da && plane_size(p) == dn)
          d = &p;
      }
    if ((cc && !c) || (cd && !d))
      return false;
    try {
      Context context(*this);
      BindFramebuffer(GL_FRAMEBUFFER, fbo_);
      FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           cc ? c->texture : 0, 0);
      FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                           GL_TEXTURE_2D, 0, 0);
      FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           0, 0);
      bool z16 = cd && ((d->desc.format >> 4) & 15) == 1;
      if (cd)
        FramebufferTexture2D(GL_FRAMEBUFFER,
                             z16 ? GL_DEPTH_ATTACHMENT
                                 : GL_DEPTH_STENCIL_ATTACHMENT,
                             GL_TEXTURE_2D, d->texture, 0);
      glDrawBuffer(cc ? GL_COLOR_ATTACHMENT0 : GL_NONE);
      glReadBuffer(cc ? GL_COLOR_ATTACHMENT0 : GL_NONE);
      glDisable(GL_SCISSOR_TEST);
      GLbitfield mask = 0;
      fill = (cc && !c->dirty) || (cd && !d->dirty);
      if (cc) {
        auto rgba = argb(cv);
        const unsigned fmt = c->desc.format & 15;
        if (fmt == 3)
          rgba = {float((cv >> 11) & 31) / 31, float((cv >> 5) & 63) / 63,
                  float(cv & 31) / 31, 1};
        if (fmt == 1 || fmt == 2)
          rgba = {float((cv >> 10) & 31) / 31, float((cv >> 5) & 31) / 31,
                  float(cv & 31) / 31, 1};
        glColorMask(true, true, true, true);
        glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
        mask |= GL_COLOR_BUFFER_BIT;
      }
      if (cd) {
        glDepthMask(true);
        glStencilMask(255);
        glClearDepth(z16 ? double(dv & 65535) / 65535
                         : double(dv >> 8) / 16777215);
        glClearStencil(dv & 255);
        mask |= GL_DEPTH_BUFFER_BIT;
        if (!z16)
          mask |= GL_STENCIL_BUFFER_BIT;
      }
      if (CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("incomplete clear framebuffer");
      glClear(mask);
      if (glGetError() != GL_NO_ERROR)
        throw std::runtime_error("GPU clear failed");
      return true;
    } catch (const std::exception &e) {
      error = e.what();
      return false;
    }
  }
  bool submit_hle_draw(const Nv2aHleDraw &p, std::string &error) override {
    try {
      Context context(*this);
      if (p.vertices.empty() || p.indices.empty())
        return true;
      auto &prog = program(p);
      UseProgram(prog.id);
      ensure_plane(p.color);
      if (p.depth.width)
        ensure_plane(p.depth);
      auto *c = plane(p.color.address, p.color.pitch, p.color.height, false);
      auto *d = p.depth.width ? plane(p.depth.address, p.depth.pitch,
                                      p.depth.height, true)
                              : nullptr;
      BindFramebuffer(GL_FRAMEBUFFER, fbo_);
      FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           c->texture, 0);
      FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                           GL_TEXTURE_2D, 0, 0);
      FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           0, 0);
      if (d)
        FramebufferTexture2D(GL_FRAMEBUFFER,
                             ((d->desc.format >> 4) & 15) == 1
                                 ? GL_DEPTH_ATTACHMENT
                                 : GL_DEPTH_STENCIL_ATTACHMENT,
                             GL_TEXTURE_2D, d->texture, 0);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
      glReadBuffer(GL_COLOR_ATTACHMENT0);
      if (CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("incomplete framebuffer");
      const auto &r = p.state;
      const unsigned enables = r[N::kRasterEnables];
      glViewport(0, 0, p.color.width, p.color.height);
      glEnable(GL_SCISSOR_TEST);
      int x = r[N::kRasterSurfaceClipHorizontal] & 65535,
          y = r[N::kRasterSurfaceClipVertical] & 65535;
      int right = x + (r[N::kRasterSurfaceClipHorizontal] >> 16),
          bottom = y + (r[N::kRasterSurfaceClipVertical] >> 16);
      x = std::max(x, int(r[N::kRasterWindowClipHorizontal] & 4095));
      y = std::max(y, int(r[N::kRasterWindowClipVertical] & 4095));
      right = std::min(
          right, int((r[N::kRasterWindowClipHorizontal] >> 16) & 4095) + 1);
      bottom = std::min(
          bottom, int((r[N::kRasterWindowClipVertical] >> 16) & 4095) + 1);
      glScissor(x, y, std::max(0, right - x), std::max(0, bottom - y));
      if (enables & 4) {
        glEnable(GL_CULL_FACE);
        glCullFace(r[N::kRasterCullFace]);
      } else
        glDisable(GL_CULL_FACE);
      glFrontFace(r[N::kRasterFrontFace] == GL_CW ? GL_CCW : GL_CW);
      if (enables & 2) {
        glEnable(GL_BLEND);
        glBlendFunc(r[N::kRasterBlendSrc], r[N::kRasterBlendDst]);
        BlendEquation(r[N::kRasterBlendEquation]);
        auto b = argb(r[N::kRasterBlendColor]);
        BlendColor(b[0], b[1], b[2], b[3]);
      } else
        glDisable(GL_BLEND);
      if (d && (enables & 8)) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(r[N::kRasterDepthFunc]);
      } else
        glDisable(GL_DEPTH_TEST);
      glDepthMask(d && r[N::kRasterDepthMask]);
      if (d && (enables & 32)) {
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(r[N::kRasterStencilFunc], r[N::kRasterStencilRef],
                      r[N::kRasterStencilFuncMask]);
        glStencilMask(r[N::kRasterStencilWriteMask]);
        glStencilOp(r[N::kRasterStencilOpFail], r[N::kRasterStencilOpZFail],
                    r[N::kRasterStencilOpZPass]);
      } else
        glDisable(GL_STENCIL_TEST);
      if (enables & 64) {
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(r[N::kRasterLogicOp]);
      } else
        glDisable(GL_COLOR_LOGIC_OP);
      const unsigned mask = r[N::kRasterColorMask];
      glColorMask((mask & 0x10000) != 0, (mask & 0x100) != 0, (mask & 1) != 0,
                  (mask & 0x1000000) != 0);
      Uniform4fv(uniform(prog, "c"), 192, p.constants[0].data());
      Uniform4fv(uniform(prog, "ambientColor"), 1, p.ambient_color.data());
      Uniform1i(uniform(prog, "ambientLighting"), p.ambient_lighting);
      Uniform1i(uniform(prog, "specularAlphaFromVertex"),
                p.specular_alpha_from_vertex);
      Uniform2f(uniform(prog, "surfaceSize"), float(p.color.width),
                float(p.color.height));
      const float max_depth =
          ((p.color.format >> 4) & 15) == 1 ? 65535.f : 16777215.f;
      float range[4]{0, max_depth, as_float(r[N::kRasterClipMin]),
                     as_float(r[N::kRasterClipMax])};
      Uniform4fv(uniform(prog, "clipRange"), 1, range);
      Uniform2i(uniform(prog, "surfaceScale"), 1, 1);
      int clip[32]{};
      for (unsigned i = 0; i < 8; ++i) {
        clip[i * 4 + 2] = p.color.width;
        clip[i * 4 + 3] = p.color.height;
      }
      Uniform4iv(uniform(prog, "clipRegion"), 8, clip);
      Uniform1i(uniform(prog, "alphaRef"), r[N::kRasterAlphaRef] & 255);
      Uniform1f(uniform(prog, "depthOffset"),
                (enables & (1u << 13))
                    ? as_float(r[N::kRasterPolygonOffsetBias])
                    : 0.f);
      Uniform1f(uniform(prog, "depthFactor"),
                (enables & (1u << 13))
                    ? as_float(r[N::kRasterPolygonOffsetScale])
                    : 0.f);
      float constants[18][4]{};
      for (unsigned i = 0; i < 8; ++i) {
        auto a = argb(r[N::kRasterCombinerFactor0_0 +
                        ((r[N::kRasterCombinerControl] & 0x1000) ? i : 0)]);
        auto b = argb(r[N::kRasterCombinerFactor1_0 +
                        ((r[N::kRasterCombinerControl] & 0x10000) ? i : 0)]);
        std::memcpy(constants[i * 2], a.data(), 16);
        std::memcpy(constants[i * 2 + 1], b.data(), 16);
      }
      auto a = argb(r[N::kRasterCombinerFinalFactor0]),
           b = argb(r[N::kRasterCombinerFinalFactor1]);
      std::memcpy(constants[16], a.data(), 16);
      std::memcpy(constants[17], b.data(), 16);
      Uniform4fv(uniform(prog, "consts"), 18, constants[0]);
      std::vector<GLuint> feedback;
      for (unsigned i = 0; i < 4; ++i) {
        const auto &t = p.textures[i];
        if (!t.present)
          continue;
        ActiveTexture(GL_TEXTURE0 + i);
        GLuint id = texture(t);
        GLenum target =
            t.cubemap ? GL_TEXTURE_CUBE_MAP
                      : (t.dimensions == 3 ? GL_TEXTURE_3D : GL_TEXTURE_2D);
        if (id == c->texture) {
          GLuint copy;
          glGenTextures(1, &copy);
          glBindTexture(GL_TEXTURE_2D, copy);
          glTexImage2D(GL_TEXTURE_2D, 0, surface_gl(c->desc).internal, t.width,
                       t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
          CopyImageSubData(id, GL_TEXTURE_2D, 0, 0, 0, 0, copy, GL_TEXTURE_2D,
                           0, 0, 0, 0, t.width, t.height, 1);
          id = copy;
          feedback.push_back(copy);
        }
        ActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(target, id);
        SamplerParameteri(samplers_[i], GL_TEXTURE_WRAP_S,
                          wrap(t.address_mode & 15));
        SamplerParameteri(samplers_[i], GL_TEXTURE_WRAP_T,
                          wrap((t.address_mode >> 8) & 15));
        if (t.cubemap || t.dimensions == 3)
          SamplerParameteri(samplers_[i], GL_TEXTURE_WRAP_R,
                            wrap((t.address_mode >> 16) & 15));
        const unsigned min = (t.filter >> 16) & 255,
                       mag = (t.filter >> 24) & 15;
        GLint min_filter = GL_NEAREST;
        switch (min) {
        case 0:
        case 1:
          break;
        case 2:
          min_filter = GL_LINEAR;
          break;
        case 3:
          min_filter = GL_NEAREST_MIPMAP_NEAREST;
          break;
        case 4:
          min_filter = GL_LINEAR_MIPMAP_NEAREST;
          break;
        case 5:
          min_filter = GL_NEAREST_MIPMAP_LINEAR;
          break;
        case 6:
          min_filter = GL_LINEAR_MIPMAP_LINEAR;
          break;
        case 7:
          min_filter = GL_LINEAR;
          break;
        default:
          throw std::runtime_error("unsupported texture minification " +
                                   std::to_string(min));
        }
        SamplerParameteri(samplers_[i], GL_TEXTURE_MIN_FILTER, min_filter);
        SamplerParameteri(samplers_[i], GL_TEXTURE_MAG_FILTER,
                          mag == 1 ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, t.levels - 1);
        if (t.resident && (t.format == 0x30 || t.format == 0x2e)) {
          GLint swiz[4]{GL_RED, t.format == 0x2e ? GL_ONE : GL_ZERO, GL_ZERO,
                        GL_ZERO};
          glTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, swiz);
        }
        auto border = argb(t.border);
        SamplerParameterfv(samplers_[i], GL_TEXTURE_BORDER_COLOR,
                           border.data());
        BindSampler(i, samplers_[i]);
        Uniform1i(uniform(prog, "texSamp" + std::to_string(i)), i);
        Uniform1f(uniform(prog, "texScale[" + std::to_string(i) + "]"), 1.f);
      }
      ActiveTexture(GL_TEXTURE0);
      std::vector<uint32_t> indices;
      const auto *draw_indices = &p.indices;
      GLenum mode;
      switch (p.primitive) {
      case 1:
        mode = GL_POINTS;
        break;
      case 2:
        mode = GL_LINES;
        break;
      case 3:
        mode = GL_LINE_LOOP;
        break;
      case 4:
        mode = GL_LINE_STRIP;
        break;
      case 5:
        mode = GL_TRIANGLES;
        break;
      case 6:
        mode = GL_TRIANGLE_STRIP;
        break;
      case 7:
      case 10:
        mode = GL_TRIANGLE_FAN;
        break;
      case 8:
      case 9:
        mode = GL_TRIANGLES;
        for (size_t i = 0; i + 3 < p.indices.size();
             i += p.primitive == 8 ? 4 : 2) {
          auto a = p.indices[i], b = p.indices[i + 1], c = p.indices[i + 2],
               d = p.indices[i + 3];
          indices.insert(
              indices.end(),
              p.primitive == 8
                  ? std::initializer_list<uint32_t>{a, b, c, a, c, d}
                  : std::initializer_list<uint32_t>{a, b, d, a, d, c});
        }
        draw_indices = &indices;
        break;
      default:
        throw std::runtime_error("primitive mode");
      }
      BindVertexArray(vao_);
      BindBuffer(GL_SHADER_STORAGE_BUFFER, vbo_);
      BufferData(GL_SHADER_STORAGE_BUFFER,
                 p.vertices.size() * sizeof(Nv2aHleDraw::Vertex),
                 p.vertices.data(), GL_STREAM_DRAW);
      BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vbo_);
      BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
      BufferData(GL_ELEMENT_ARRAY_BUFFER, draw_indices->size() * 4,
                 draw_indices->data(), GL_STREAM_DRAW);
      glDrawElements(mode, GLsizei(draw_indices->size()), GL_UNSIGNED_INT,
                     nullptr);
      if ((mask & 0x01010101) && right > x && bottom > y)
        c->dirty = true;
      if (d && right > x && bottom > y &&
          (((enables & 8) && r[N::kRasterDepthMask]) || (enables & 32)))
        d->dirty = true;
      for (auto id : feedback)
        glDeleteTextures(1, &id);
      GLenum err = glGetError();
      if (err != GL_NO_ERROR)
        throw std::runtime_error("OpenGL error " + std::to_string(err));
      ++draws_;
      vertices_ += p.vertices.size();
      return true;
    } catch (const std::exception &e) {
      error = e.what();
      return false;
    }
  }
};
} // namespace
std::unique_ptr<Nv2aRenderer> create_nv2a_hle_renderer(std::string &error) {
  auto renderer = std::make_unique<HleRenderer>();
  if (!renderer->initialize(error))
    return {};
  return renderer;
}
} // namespace xlle
