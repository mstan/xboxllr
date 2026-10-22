// SPDX-License-Identifier: GPL-3.0-or-later
// Render order, resource layout and original-look programs adapted from the
// MIT xbox-boot-re WebGPU host, commit 12a3396. See its retained license.
#include "boot_renderer.h"
#include "boot_shaders.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <fstream>
#include <stdexcept>

namespace {
void check(HRESULT result, const char *operation) {
    if (FAILED(result))
        throw std::runtime_error(std::string(operation) +
                                 " failed: " + std::to_string((unsigned)result));
}
void core_check(int result, const char *operation) {
    if (result != XB_OK)
        throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
}
ComPtr<ID3DBlob> compile(const std::string &entry, const char *profile) {
    ComPtr<ID3DBlob> code, errors;
    HRESULT result = D3DCompile(
        BOOT_HLSL, std::strlen(BOOT_HLSL), "xboxllr-native-boot", nullptr, nullptr, entry.c_str(),
        profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(result)) {
        std::string detail =
            errors ? std::string((char *)errors->GetBufferPointer(), errors->GetBufferSize()) : "";
        throw std::runtime_error("Shader " + entry + ": " + detail);
    }
    return code;
}
unsigned mesh_key(unsigned mesh, unsigned detail = 0) {
    return mesh * 2 + detail;
}
void matrix_multiply(const float *a, const float *b, double *out) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            double v = 0;
            for (int k = 0; k < 4; ++k)
                v += (double)a[4 * r + k] * b[4 * k + c];
            out[4 * r + c] = v;
        }
}
D3D11_BLEND factor(unsigned value) {
    switch (value) {
    case 0:
        return D3D11_BLEND_ZERO;
    case 1:
        return D3D11_BLEND_ONE;
    case 0x302:
        return D3D11_BLEND_SRC_ALPHA;
    case 0x303:
        return D3D11_BLEND_INV_SRC_ALPHA;
    default:
        throw std::runtime_error("Unsupported startup blend factor");
    }
}
} // namespace

BootRenderer::Surface BootRenderer::make_surface(unsigned width, unsigned height, bool depth,
                                                 bool depth16) {
    Surface result;
    result.width = width;
    result.height = height;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.ArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.Format = depth ? (depth16 ? DXGI_FORMAT_R16_TYPELESS : DXGI_FORMAT_R32_TYPELESS)
                        : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | (depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET);
    check(device_->CreateTexture2D(&desc, nullptr, &result.texture), "Create startup surface");
    if (depth) {
        D3D11_DEPTH_STENCIL_VIEW_DESC view{};
        view.Format = depth16 ? DXGI_FORMAT_D16_UNORM : DXGI_FORMAT_D32_FLOAT;
        view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        check(device_->CreateDepthStencilView(result.texture.Get(), &view, &result.dsv),
              "Create startup depth view");
    } else {
        check(device_->CreateRenderTargetView(result.texture.Get(), nullptr, &result.rtv),
              "Create startup render target");
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = depth ? (depth16 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R32_FLOAT) : desc.Format;
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = 1;
    check(device_->CreateShaderResourceView(result.texture.Get(), &view, &result.srv),
          "Create startup resource view");
    return result;
}

BootRenderer::BootRenderer(ID3D11Device *device, ID3D11DeviceContext *context, xb_state *state)
    : device_(device), context_(context), state_(state), draws_(XB_FRAME_DRAWS_MAX),
      params_(XB_FRAME_PARAMS_MAX) {
    surfaces_[XB_SURF_LIGHT0] = make_surface(512, 512, true, true);
    surfaces_[XB_SURF_LIGHT1] = make_surface(512, 512, true, true);
    surfaces_[XB_SURF_FX_A] = make_surface(1024, 512, false);
    surfaces_[XB_SURF_FX_B] = make_surface(1024, 512, false);
    surfaces_[XB_SURF_FX_DEPTH] = make_surface(1024, 512, true);
    surfaces_[XB_SURF_LOGO] = make_surface(1024, 1024, false);
    surfaces_[XB_SURF_LOGO_TARGET] = make_surface(1024, 1024, false);
    surfaces_[XB_SURF_LOGO_DEPTH] = make_surface(1024, 1024, true);
    surfaces_[XB_SURF_SCREEN] = make_surface(1280, 480, false);
    surfaces_[XB_SURF_SCREEN_DEPTH] = make_surface(1280, 480, true);
    surfaces_[XB_SURF_DISPLAY] = make_surface(640, 480, false);
    const float black[4] = {0, 0, 0, 1};
    for (unsigned id : {XB_SURF_LOGO, XB_SURF_LOGO_TARGET, XB_SURF_SCREEN, XB_SURF_DISPLAY})
        context_->ClearRenderTargetView(surfaces_[id].rtv.Get(), black);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(Uniforms);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    check(device_->CreateBuffer(&bd, nullptr, &constants_), "Create startup constants");
    bd.ByteWidth = 4 * sizeof(float) * XB_BLOB_VERTICES;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    check(device_->CreateBuffer(&bd, nullptr, &deformed_), "Create startup deformation");
    bd.ByteWidth = 256 * 1024;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    check(device_->CreateBuffer(&bd, nullptr, &indices_), "Create startup indices");
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = 0;
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    check(device_->CreateSamplerState(&sd, &clamp_), "Create startup clamp sampler");
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    check(device_->CreateSamplerState(&sd, &repeat_), "Create startup repeat sampler");
    make_meshes();
    make_textures();
    make_shaders();
}

void BootRenderer::upload_mesh(Mesh &mesh) {
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (UINT)(mesh.vertices.size() * sizeof(float));
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = mesh.vertices.data();
    check(device_->CreateBuffer(&bd, &data, &mesh.vb), "Upload startup mesh");
    if (!mesh.attributes.empty()) {
        bd.ByteWidth = (UINT)(mesh.attributes.size() * sizeof(float));
        data.pSysMem = mesh.attributes.data();
        check(device_->CreateBuffer(&bd, &data, &mesh.ab), "Upload startup attributes");
    }
}

void BootRenderer::make_meshes() {
    for (unsigned group = 0; group < XB_CHAMBER_GROUPS; ++group)
        for (unsigned detail = 0; detail < 2; ++detail) {
            xb_mesh_info info{};
            core_check(xb_chamber_mesh_info(state_, group, detail, &info), "Chamber mesh info");
            Mesh mesh;
            mesh.stride = 12;
            mesh.attr_stride = info.attr_floats * sizeof(float);
            mesh.vertices.resize(info.vertices * 3);
            mesh.attributes.resize(info.vertices * info.attr_floats);
            mesh.indices.resize(info.indices);
            std::vector<xb_form> forms(info.forms);
            core_check(xb_chamber_mesh(state_, group, detail, mesh.vertices.data(),
                                       mesh.attributes.data(), mesh.indices.data(), forms.data()),
                       "Chamber mesh");
            upload_mesh(mesh);
            meshes_.emplace(mesh_key(XB_MESH_CHAMBER + group, detail), std::move(mesh));
        }
    for (unsigned small = 0; small < 2; ++small) {
        Mesh mesh;
        mesh.stride = 12;
        mesh.vertices.resize((small ? XB_BLOB_SMALL_VERTICES : XB_BLOB_VERTICES) * 3);
        mesh.indices.resize(small ? XB_BLOB_SMALL_INDICES : XB_BLOB_INDICES);
        core_check(xb_blob_mesh(state_, small, mesh.vertices.data(), mesh.indices.data()),
                   "Blob mesh");
        upload_mesh(mesh);
        meshes_.emplace(mesh_key(small ? XB_MESH_DROP : XB_MESH_BLOB), std::move(mesh));
    }
    for (unsigned part = 0; part < XB_LOGO_MESHES; ++part) {
        xb_logo_mesh_desc info{};
        core_check(xb_logo_mesh_info(state_, part, &info), "Logo mesh info");
        Mesh mesh;
        mesh.stride = info.floats * sizeof(float);
        mesh.vertices.resize(info.vertices * info.floats);
        mesh.indices.resize(info.indices);
        core_check(xb_logo_mesh(state_, part, mesh.vertices.data(), mesh.indices.data()),
                   "Logo mesh");
        upload_mesh(mesh);
        meshes_.emplace(mesh_key(XB_MESH_LOGO + part), std::move(mesh));
    }
    float veil[4][7]{}, quad[4][3]{};
    core_check(xb_fx_quads(veil, quad), "Effects quads");
    Mesh v;
    v.stride = 28;
    v.vertices.assign(&veil[0][0], &veil[0][0] + 28);
    upload_mesh(v);
    meshes_.emplace(mesh_key(XB_MESH_VEIL), std::move(v));
    Mesh q;
    q.stride = 12;
    q.vertices.assign(&quad[0][0], &quad[0][0] + 12);
    upload_mesh(q);
    meshes_.emplace(mesh_key(XB_MESH_FX_QUAD), std::move(q));
    Mesh in;
    in.stride = 20;
    in.vertices.resize(20);
    upload_mesh(in);
    meshes_.emplace(mesh_key(XB_MESH_INLINE), std::move(in));
}

void BootRenderer::make_textures() {
    for (unsigned tex = 0; tex < XB_TEXTURES; ++tex) {
        xb_texture_desc td{};
        core_check(xb_texture_info(state_, tex, &td), "Texture info");
        if ((td.format != 6 && td.format != 7 && td.format != 0x19) ||
            (td.texel_bytes != 4 && td.texel_bytes != 1))
            throw std::runtime_error("Unsupported startup texture format");
        std::vector<uint8_t> source(td.bytes);
        core_check(xb_texture(state_, tex, source.data(), source.size()), "Generate texture");
        std::vector<std::vector<uint8_t>> rows(td.faces);
        std::vector<D3D11_SUBRESOURCE_DATA> sub(td.faces);
        for (unsigned face = 0; face < td.faces; ++face) {
            auto &row = rows[face];
            row.resize(td.width * td.height * td.texel_bytes);
            for (unsigned y = 0; y < td.height; ++y)
                for (unsigned x = 0; x < td.width; ++x) {
                    unsigned at = 0, bit = 0;
                    for (unsigned b = 0; (1u << b) < td.width || (1u << b) < td.height; ++b) {
                        if ((1u << b) < td.width)
                            at |= ((x >> b) & 1u) << bit++;
                        if ((1u << b) < td.height)
                            at |= ((y >> b) & 1u) << bit++;
                    }
                    unsigned from = face * (td.bytes / td.faces) + at * td.texel_bytes,
                             to = (y * td.width + x) * td.texel_bytes;
                    if (td.texel_bytes == 1)
                        row[to] = source[from];
                    else {
                        row[to] = source[from + 2];
                        row[to + 1] = source[from + 1];
                        row[to + 2] = source[from];
                        row[to + 3] = td.format == 7 ? 255 : source[from + 3];
                    }
                }
            sub[face].pSysMem = row.data();
            sub[face].SysMemPitch = td.width * td.texel_bytes;
        }
        Surface surface;
        surface.width = td.width;
        surface.height = td.height;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = td.width;
        desc.Height = td.height;
        desc.ArraySize = td.faces;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.Format = td.texel_bytes == 1 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.MiscFlags = td.faces == 6 ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
        check(device_->CreateTexture2D(&desc, sub.data(), &surface.texture),
              "Upload startup texture");
        check(device_->CreateShaderResourceView(surface.texture.Get(), nullptr, &surface.srv),
              "View startup texture");
        surfaces_[XB_SURF_TEX + tex] = std::move(surface);
    }
}

void BootRenderer::make_shaders() {
    for (const std::string name : {"smooth", "relief", "lightdepth", "depth", "blob", "drop",
                                   "veil", "logod", "fixed", "plain", "present", "window"}) {
        Shader sh;
        auto vs = compile(name == "window" ? "present_vs" : name + "_vs", "vs_5_0");
        check(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr,
                                          &sh.vs),
              "Create startup vertex shader");
        if (name != "lightdepth") {
            auto ps = compile(name + "_ps", "ps_5_0");
            check(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr,
                                             &sh.ps),
                  "Create startup pixel shader");
        }
        std::vector<D3D11_INPUT_ELEMENT_DESC> input;
        auto at = [&](const char *semantic, UINT index, UINT slot, UINT offset,
                      DXGI_FORMAT format) {
            input.push_back(
                {semantic, index, format, slot, offset, D3D11_INPUT_PER_VERTEX_DATA, 0});
        };
        if (name != "present" && name != "window")
            at("POSITION", 0, 0, 0, DXGI_FORMAT_R32G32B32_FLOAT);
        if (name == "smooth" || name == "relief") {
            unsigned off = name == "relief" ? 8 : 0;
            if (off)
                at("TEXCOORD", 0, 1, 0, DXGI_FORMAT_R32G32_FLOAT);
            for (UINT i = 0; i < 3; ++i)
                at("NORMAL", i, 1, off + 12 * i, DXGI_FORMAT_R32G32B32_FLOAT);
        } else if (name == "blob")
            at("NORMAL", 0, 1, 0, DXGI_FORMAT_R32G32B32A32_FLOAT);
        else if (name == "veil") {
            at("TEXCOORD", 0, 0, 12, DXGI_FORMAT_R32G32_FLOAT);
            at("TEXCOORD", 1, 0, 20, DXGI_FORMAT_R32G32_FLOAT);
        } else if (name == "fixed" || name == "logod")
            at("TEXCOORD", 0, 0, 12, DXGI_FORMAT_R32G32_FLOAT);
        if (!input.empty())
            check(device_->CreateInputLayout(input.data(), (UINT)input.size(),
                                             vs->GetBufferPointer(), vs->GetBufferSize(),
                                             &sh.layout),
                  "Create startup input layout");
        shaders_.emplace(name, std::move(sh));
    }
}

const std::string BootRenderer::kind(const xb_draw &draw) const {
    if (draw.step == XB_STEP_U1)
        return "lightdepth";
    switch (draw.program) {
    case 0x80065B60:
        return "smooth";
    case 0x80065998:
        return "relief";
    case 0x80065D28:
        return "depth";
    case 0x800662E8:
        return "blob";
    case 0x80066470:
        return "drop";
    case 0x80066690:
        return "veil";
    case 0x80066868:
        return "logod";
    default:
        return draw.what == XB_DRAW_LOGO_E ? "plain" : "fixed";
    }
}

void BootRenderer::states(const xb_draw &draw, bool light) {
    const auto &s = draw.state;
    unsigned dk = (s.depth_test ? 1u : 0u) | (s.depth_test && s.depth_write ? 2u : 0u) |
                  ((s.depth_func & 7u) << 2);
    auto &depth = depth_states_[dk];
    if (!depth) {
        D3D11_DEPTH_STENCIL_DESC d{};
        d.DepthEnable = s.depth_test != 0;
        d.DepthWriteMask = s.depth_test && s.depth_write ? D3D11_DEPTH_WRITE_MASK_ALL
                                                         : D3D11_DEPTH_WRITE_MASK_ZERO;
        d.DepthFunc = (D3D11_COMPARISON_FUNC)(1 + (s.depth_func & 7));
        check(device_->CreateDepthStencilState(&d, &depth), "Create startup depth state");
    }
    context_->OMSetDepthStencilState(depth.Get(), 0);
    unsigned bk = s.blend_src | (s.blend_dst << 12) | (s.blend ? 1u << 24 : 0u) |
                  (s.colour_mask ? 1u << 25 : 0u);
    auto &blend = blend_states_[bk];
    if (!blend) {
        D3D11_BLEND_DESC d{};
        auto &r = d.RenderTarget[0];
        r.BlendEnable = s.blend != 0;
        r.SrcBlend = r.SrcBlendAlpha = s.blend ? factor(s.blend_src) : D3D11_BLEND_ONE;
        r.DestBlend = r.DestBlendAlpha = s.blend ? factor(s.blend_dst) : D3D11_BLEND_ZERO;
        r.BlendOp = r.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        r.RenderTargetWriteMask = s.colour_mask ? D3D11_COLOR_WRITE_ENABLE_ALL : 0;
        check(device_->CreateBlendState(&d, &blend), "Create startup blend state");
    }
    context_->OMSetBlendState(blend.Get(), nullptr, 0xffffffffu);
    unsigned rk = (s.cull ? (s.cull_face == 0x404 ? 1u : 2u) : 0u) | (light ? 4u : 0u);
    auto &raster = raster_states_[rk];
    if (!raster) {
        D3D11_RASTERIZER_DESC d{};
        d.FillMode = D3D11_FILL_SOLID;
        d.CullMode = !s.cull                ? D3D11_CULL_NONE
                     : s.cull_face == 0x404 ? D3D11_CULL_FRONT
                                            : D3D11_CULL_BACK;
        d.DepthClipEnable = TRUE;
        d.FrontCounterClockwise = FALSE;
        d.DepthBias = light ? 10 : 0;
        d.SlopeScaledDepthBias = light ? 4.0f : 0;
        check(device_->CreateRasterizerState(&d, &raster), "Create startup raster state");
    }
    context_->RSSetState(raster.Get());
}

bool BootRenderer::uniforms(const xb_draw &draw, Uniforms &out) {
    std::memset(&out, 0, sizeof(out));
    if (draw.flags & XB_DRAW_MATRICES) {
        double wv[16]{};
        matrix_multiply(params_.data() + draw.world, params_.data() + draw.view, wv);
        const float *p = params_.data() + draw.proj;
        for (unsigned r = 0; r < 4; ++r)
            for (unsigned c = 0; c < 4; ++c) {
                double v = 0;
                for (unsigned k = 0; k < 4; ++k)
                    v += wv[4 * r + k] * p[4 * k + c];
                out.c[c][r] = (float)v;
            }
    } else
        for (const auto &run : draw.vs)
            for (unsigned i = 0; i < run.count && run.base + i < 20; ++i)
                std::memcpy(out.c[run.base + i], params_.data() + run.first + 4 * i, 16);
    for (const auto &row : out.c)
        for (float v : row)
            if (std::isnan(v))
                return false;
    for (unsigned i = 0; i < 4 * std::min(draw.ps_count, 3u); ++i) {
        float v = std::clamp(params_[draw.ps_first + i], 0.0f, 1.0f);
        out.k[i / 4][i % 4] = std::trunc(v * 255.0f + 0.5f) / 255.0f;
    }
    auto k = kind(draw);
    if (k == "fixed" || k == "plain") {
        uint32_t f = draw.flags & XB_DRAW_FACTOR ? draw.factor : 0xffffffffu;
        out.k[3][0] = ((f >> 16) & 255) / 255.0f;
        out.k[3][1] = ((f >> 8) & 255) / 255.0f;
        out.k[3][2] = (f & 255) / 255.0f;
        out.k[3][3] = (f >> 24) / 255.0f;
        out.k[2][3] = 1;
    }
    out.bias[0] = 0.53125f;
    out.bias[2] = surfaces_[draw.zeta].width * 0.5f;
    out.bias[3] = surfaces_[draw.zeta].height * 0.5f;
    if (draw.state.alpha_test && draw.state.alpha_func != 0x206)
        throw std::runtime_error("Unsupported startup alpha function");
    out.misc[0] = (float)draw.state.alpha_ref;
    out.misc[1] = draw.what == XB_DRAW_BLOB_GLOW                                 ? 3.0f
                  : draw.what == XB_DRAW_GLOW                                    ? 7.0f
                  : draw.what == XB_DRAW_LOGO_Q1 || draw.what == XB_DRAW_LOGO_Q2 ? 13.0f
                                                                                 : 9.0f;
    out.misc[2] = (float)draw.state.alpha_test;
    out.misc[3] = draw.state.shadow_func == 0x204 ? 1.0f : 0;
    return true;
}

void BootRenderer::submit(const xb_draw &draw) {
    unsigned detail = draw.mesh >= XB_MESH_CHAMBER && draw.mesh < XB_MESH_BLOB ? draw.detail : 0;
    auto found = meshes_.find(mesh_key(draw.mesh, detail));
    if (found == meshes_.end())
        throw std::runtime_error("Missing startup mesh");
    Mesh &mesh = found->second;
    Uniforms values{};
    if (!uniforms(draw, values))
        return;
    auto k = kind(draw);
    auto &shader = shaders_.at(k);
    if (draw.mesh == XB_MESH_INLINE) {
        if (draw.vtx_count > 20)
            throw std::runtime_error("Oversized startup inline mesh");
        std::copy_n(params_.data() + draw.vtx_first, draw.vtx_count, mesh.vertices.data());
        context_->UpdateSubresource(mesh.vb.Get(), 0, nullptr, mesh.vertices.data(), 0, 0);
    }
    auto index = [&](unsigned i) -> unsigned {
        if (!(draw.flags & XB_DRAW_INDEXED))
            return draw.first + i;
        if (draw.first + i >= mesh.indices.size())
            throw std::runtime_error("Startup index outside mesh");
        return mesh.indices[draw.first + i];
    };
    std::vector<uint32_t> tri;
    tri.reserve(draw.count * 3);
    if (draw.prim == 5)
        for (unsigned i = 0; i < draw.count - draw.count % 3; ++i)
            tri.push_back(index(i));
    else if (draw.prim == 6 || draw.prim == 7)
        for (unsigned i = 2; i < draw.count; ++i) {
            if (draw.prim == 7) {
                tri.push_back(index(0));
                tri.push_back(index(i - 1));
            } else if (i & 1) {
                tri.push_back(index(i - 1));
                tri.push_back(index(i - 2));
            } else {
                tri.push_back(index(i - 2));
                tri.push_back(index(i - 1));
            }
            tri.push_back(index(i));
        }
    else
        throw std::runtime_error("Unsupported startup primitive");
    if (tri.size() * 4 > 256 * 1024)
        throw std::runtime_error("Startup index capacity exceeded");
    for (unsigned i : tri)
        if ((size_t)i * mesh.stride >= mesh.vertices.size() * sizeof(float))
            throw std::runtime_error("Startup vertex outside mesh");
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context_->Map(indices_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped),
          "Map startup indices");
    std::memcpy(mapped.pData, tri.data(), tri.size() * 4);
    context_->Unmap(indices_.Get(), 0);
    check(context_->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped),
          "Map startup constants");
    std::memcpy(mapped.pData, &values, sizeof(values));
    context_->Unmap(constants_.Get(), 0);
    ID3D11Buffer *cb = constants_.Get();
    context_->VSSetConstantBuffers(0, 1, &cb);
    context_->PSSetConstantBuffers(0, 1, &cb);
    context_->VSSetShader(shader.vs.Get(), nullptr, 0);
    context_->PSSetShader(shader.ps.Get(), nullptr, 0);
    context_->IASetInputLayout(shader.layout.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer *vb[2] = {mesh.vb.Get(), k == "blob" ? deformed_.Get() : mesh.ab.Get()};
    UINT strides[2] = {mesh.stride, k == "blob" ? 16u : mesh.attr_stride}, offsets[2] = {0, 0};
    context_->IASetVertexBuffers(0, 2, vb, strides, offsets);
    context_->IASetIndexBuffer(indices_.Get(), DXGI_FORMAT_R32_UINT, 0);
    ID3D11ShaderResourceView *tex[6]{};
    auto view = [&](unsigned id) { return surfaces_.at(id).srv.Get(); };
    if (k == "smooth" || k == "relief") {
        tex[4] = view(draw.tex[1]);
        tex[5] = view(draw.tex[3]);
        if (k == "relief")
            tex[0] = view(draw.tex[0]);
    } else if (k == "blob" || k == "drop")
        tex[4] = view(draw.tex[0]);
    else if (k == "veil")
        for (unsigned i = 0; i < 4; ++i)
            tex[i] = view(draw.tex[i]);
    else if (k == "fixed")
        tex[0] = view(draw.tex[0]);
    context_->PSSetShaderResources(0, 6, tex);
    ID3D11SamplerState *sam[2] = {clamp_.Get(), repeat_.Get()};
    context_->PSSetSamplers(0, 2, sam);
    states(draw, k == "lightdepth");
    context_->DrawIndexed((UINT)tri.size(), 0, 0);
    ++submitted_;
}

void BootRenderer::draw() {
    xb_frame_out frame{};
    frame.draws = draws_.data();
    frame.draw_cap = (uint32_t)draws_.size();
    frame.params = params_.data();
    frame.param_cap = (uint32_t)params_.size();
    core_check(xb_frame(state_, &frame), "Describe startup frame");
    float deform[XB_BLOB_VERTICES][4]{};
    core_check(xb_blob_deformed(state_, deform), "Deform startup blob");
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context_->Map(deformed_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped),
          "Map startup deformation");
    std::memcpy(mapped.pData, deform, sizeof(deform));
    context_->Unmap(deformed_.Get(), 0);
    bool begun[XB_SURFACES]{};
    bool cleared = !frame.clear_depth;
    unsigned previous_color = 0, previous_depth = 0;
    ID3D11ShaderResourceView *nulls[6]{};
    auto finish = [&] {
        context_->PSSetShaderResources(0, 6, nulls);
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        if (previous_color == XB_SURF_LOGO_TARGET)
            context_->CopyResource(surfaces_[XB_SURF_LOGO].texture.Get(),
                                   surfaces_[XB_SURF_LOGO_TARGET].texture.Get());
    };
    for (unsigned i = 0; i < frame.draw_count; ++i) {
        const auto &d = draws_[i];
        if (d.what == XB_DRAW_PRESENT || d.color == XB_SURF_DISPLAY || !surfaces_[d.zeta].dsv)
            continue;
        bool clear_now = d.color == XB_SURF_SCREEN && d.step == XB_STEP_D3 && !cleared;
        if (d.color != previous_color || d.zeta != previous_depth || clear_now) {
            finish();
            auto &color = surfaces_[d.color];
            auto &depth = surfaces_[d.zeta];
            if (!begun[d.zeta]) {
                const float black[4] = {0, 0, 0, 1}, red[4] = {1, 0, 0, 1};
                if (color.rtv)
                    context_->ClearRenderTargetView(
                        color.rtv.Get(),
                        d.color == XB_SURF_FX_A || d.color == XB_SURF_FX_B ? red : black);
            }
            if (!begun[d.zeta] || clear_now)
                context_->ClearDepthStencilView(depth.dsv.Get(), D3D11_CLEAR_DEPTH, 1, 0);
            begun[d.zeta] = true;
            cleared |= clear_now;
            ID3D11RenderTargetView *rtv = color.rtv.Get();
            context_->OMSetRenderTargets(rtv ? 1 : 0, rtv ? &rtv : nullptr, depth.dsv.Get());
            D3D11_VIEWPORT vp{0, 0, (float)depth.width, (float)depth.height, 0, 1};
            context_->RSSetViewports(1, &vp);
            previous_color = d.color;
            previous_depth = d.zeta;
        }
        submit(d);
    }
    finish();
    if (!begun[XB_SURF_SCREEN_DEPTH]) {
        const float black[4] = {0, 0, 0, 1};
        context_->ClearRenderTargetView(surfaces_[XB_SURF_SCREEN].rtv.Get(), black);
    }
    auto &display = surfaces_[XB_SURF_DISPLAY];
    ID3D11RenderTargetView *output = display.rtv.Get();
    context_->OMSetRenderTargets(1, &output, nullptr);
    D3D11_VIEWPORT viewport{0, 0, 640, 480, 0, 1};
    context_->RSSetViewports(1, &viewport);
    xb_draw present{};
    present.state.colour_mask = 0x01010101;
    present.state.depth_func = 0x207;
    states(present, false);
    context_->IASetInputLayout(nullptr);
    auto &shader = shaders_.at("present");
    context_->VSSetShader(shader.vs.Get(), nullptr, 0);
    context_->PSSetShader(shader.ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView *screen = surfaces_[XB_SURF_SCREEN].srv.Get();
    context_->PSSetShaderResources(0, 1, &screen);
    context_->Draw(3, 0);
    context_->PSSetShaderResources(0, 6, nulls);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
}

void BootRenderer::show(ID3D11RenderTargetView *output, unsigned width, unsigned height) {
    const float black[4] = {0, 0, 0, 1};
    context_->ClearRenderTargetView(output, black);
    float scale = std::min(width / 640.0f, height / 480.0f), w = 640 * scale, h = 480 * scale,
          x = (width - w) * 0.5f, y = (height - h) * 0.5f;
    Uniforms values{};
    values.bias[0] = x;
    values.bias[1] = y;
    values.bias[2] = w;
    values.bias[3] = h;
    D3D11_MAPPED_SUBRESOURCE map{};
    check(context_->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map),
          "Map startup window constants");
    std::memcpy(map.pData, &values, sizeof(values));
    context_->Unmap(constants_.Get(), 0);
    ID3D11Buffer *cb = constants_.Get();
    context_->PSSetConstantBuffers(0, 1, &cb);
    context_->OMSetRenderTargets(1, &output, nullptr);
    D3D11_VIEWPORT vp{x, y, w, h, 0, 1};
    context_->RSSetViewports(1, &vp);
    xb_draw present{};
    present.state.colour_mask = 0x01010101;
    present.state.depth_func = 0x207;
    states(present, false);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto &shader = shaders_.at("window");
    context_->VSSetShader(shader.vs.Get(), nullptr, 0);
    context_->PSSetShader(shader.ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView *display = surfaces_[XB_SURF_DISPLAY].srv.Get();
    context_->PSSetShaderResources(0, 1, &display);
    ID3D11SamplerState *sampler = clamp_.Get();
    context_->PSSetSamplers(0, 1, &sampler);
    context_->Draw(3, 0);
    ID3D11ShaderResourceView *nulls[6]{};
    context_->PSSetShaderResources(0, 6, nulls);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
}

void BootRenderer::capture(const std::wstring &path) {
    read_surface(surfaces_[XB_SURF_DISPLAY], path);
}

void BootRenderer::capture_presentation(const std::wstring &path, unsigned width, unsigned height) {
    Surface target = make_surface(width, height, false);
    show(target.rtv.Get(), width, height);
    read_surface(target, path);
}

void BootRenderer::read_surface(const Surface &surface, const std::wstring &path) {
    D3D11_TEXTURE2D_DESC desc{};
    surface.texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> read;
    check(device_->CreateTexture2D(&desc, nullptr, &read), "Create startup readback");
    context_->CopyResource(read.Get(), surface.texture.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    check(context_->Map(read.Get(), 0, D3D11_MAP_READ, 0, &map), "Read startup picture");
    BITMAPFILEHEADER header{};
    BITMAPINFOHEADER info{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info);
    header.bfSize = header.bfOffBits + desc.Width * desc.Height * 4;
    info.biSize = sizeof(info);
    info.biWidth = desc.Width;
    info.biHeight = -(LONG)desc.Height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = desc.Width * desc.Height * 4;
    std::ofstream stream(path, std::ios::binary);
    stream.write((char *)&header, sizeof(header));
    stream.write((char *)&info, sizeof(info));
    std::vector<uint8_t> row(desc.Width * 4);
    for (unsigned y = 0; y < desc.Height; ++y) {
        const uint8_t *src = (uint8_t *)map.pData + y * map.RowPitch;
        for (unsigned x = 0; x < desc.Width; ++x) {
            row[4 * x] = src[4 * x + 2];
            row[4 * x + 1] = src[4 * x + 1];
            row[4 * x + 2] = src[4 * x];
            row[4 * x + 3] = 255;
        }
        stream.write((char *)row.data(), row.size());
    }
    context_->Unmap(read.Get(), 0);
    if (!stream)
        throw std::runtime_error("Cannot write startup capture");
}

unsigned BootRenderer::nonblack_pixels() {
    D3D11_TEXTURE2D_DESC desc{};
    surfaces_[XB_SURF_DISPLAY].texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> read;
    check(device_->CreateTexture2D(&desc, nullptr, &read), "Create startup verification readback");
    context_->CopyResource(read.Get(), surfaces_[XB_SURF_DISPLAY].texture.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    check(context_->Map(read.Get(), 0, D3D11_MAP_READ, 0, &map), "Verify startup picture");
    unsigned pixels = 0;
    for (unsigned y = 0; y < desc.Height; ++y) {
        const uint8_t *row = (uint8_t *)map.pData + y * map.RowPitch;
        for (unsigned x = 0; x < desc.Width; ++x)
            if (row[4 * x] || row[4 * x + 1] || row[4 * x + 2])
                ++pixels;
    }
    context_->Unmap(read.Get(), 0);
    return pixels;
}
