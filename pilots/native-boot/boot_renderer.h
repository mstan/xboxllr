// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <d3d11.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>
#ifdef small
#undef small // Win32 RPC annotation conflicts with the portable core's name.
#endif
#include "xboot.h"

using Microsoft::WRL::ComPtr;

// Presentation-only owner. It consumes the core's ordered draw contract and
// never accesses guest RAM, kernel services, console identity or save storage.
class BootRenderer {
  public:
    BootRenderer(ID3D11Device *device, ID3D11DeviceContext *context, xb_state *state);
    BootRenderer(const BootRenderer &) = delete;
    BootRenderer &operator=(const BootRenderer &) = delete;
    void draw();
    void show(ID3D11RenderTargetView *output, unsigned width, unsigned height);
    void capture(const std::wstring &path);
    void capture_presentation(const std::wstring &path, unsigned width, unsigned height);
    unsigned nonblack_pixels();
    unsigned long long draw_count() const {
        return submitted_;
    }

  private:
    struct Surface {
        unsigned width = 0, height = 0;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11DepthStencilView> dsv;
    };
    struct Mesh {
        std::vector<float> vertices, attributes;
        std::vector<uint16_t> indices;
        unsigned stride = 0, attr_stride = 0;
        ComPtr<ID3D11Buffer> vb, ab;
    };
    struct Shader {
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        ComPtr<ID3D11InputLayout> layout;
    };
    struct Uniforms {
        float c[20][4], k[4][4], bias[4], misc[4];
    };
    ID3D11Device *device_;
    ID3D11DeviceContext *context_;
    xb_state *state_;
    std::array<Surface, XB_SURFACES> surfaces_;
    std::map<unsigned, Mesh> meshes_;
    std::map<std::string, Shader> shaders_;
    ComPtr<ID3D11Buffer> constants_, deformed_, indices_;
    ComPtr<ID3D11SamplerState> clamp_, repeat_;
    std::map<unsigned, ComPtr<ID3D11DepthStencilState>> depth_states_;
    std::map<unsigned, ComPtr<ID3D11BlendState>> blend_states_;
    std::map<unsigned, ComPtr<ID3D11RasterizerState>> raster_states_;
    std::vector<xb_draw> draws_;
    std::vector<float> params_;
    unsigned long long submitted_ = 0;
    Surface make_surface(unsigned width, unsigned height, bool depth, bool depth16 = false);
    void upload_mesh(Mesh &mesh);
    void make_meshes();
    void make_textures();
    void make_shaders();
    const std::string kind(const xb_draw &draw) const;
    void states(const xb_draw &draw, bool light_depth);
    bool uniforms(const xb_draw &draw, Uniforms &result);
    void submit(const xb_draw &draw);
    void read_surface(const Surface &surface, const std::wstring &path);
};
