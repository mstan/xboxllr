#include "third_party/phobos/rhi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <d3d11.h>
#include <vector>
#include <windows.h>
static void check(bool ok, const char *why) {
  if (!ok) {
    std::fprintf(stderr, "phobos RHI: %s\n", why);
    std::exit(1);
  }
}
int main() {
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                    0, nullptr, 0, D3D11_SDK_VERSION, &device,
                                    nullptr, &context)),
        "hardware device");
  check(!rhi_d3d11_adopt(device, context), "adopt device");
  RhiImageDesc desc{};
  desc.width = 32;
  desc.height = 32;
  desc.depth = 1;
  desc.mip_levels = 1;
  desc.samples = 1;
  desc.format = RHI_FORMAT_R8G8B8A8_UNORM;
  desc.bind = RHI_BIND_RENDER_TARGET | RHI_BIND_SAMPLED;
  auto *target = rhi_image_create(&desc, nullptr);
  check(target, "image");
  auto *view = rhi_view_create(target, RHI_VIEW_RENDER_TARGET, nullptr);
  check(view, "target view");
  const float clear[4] = {0, 0, 1, 1};
  rhi_clear_color(view, clear);
  rhi_set_render_target(view, nullptr);
  RhiViewport viewport{0, 0, 32, 32, 0, 1};
  rhi_set_viewports(1, &viewport);
  const char *vs = R"(float4 main(uint id : SV_VertexID) : SV_POSITION {
    float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)}; return float4(p[id],0,1); })";
  const char *ps = "float4 main() : SV_TARGET { return float4(1,0.25,0,1); }";
  char error[8192]{};
  RhiShaderSource vsrc{
      vs, std::strlen(vs), "contract-vs", nullptr, "main", "vs_5_0", 1};
  RhiShaderSource psrc{
      ps, std::strlen(ps), "contract-ps", nullptr, "main", "ps_5_0", 1};
  auto *vertex =
      rhi_shader_create(RHI_STAGE_VERTEX, &vsrc, error, sizeof(error));
  check(vertex, error);
  auto *pixel = rhi_shader_create(RHI_STAGE_PIXEL, &psrc, error, sizeof(error));
  check(pixel, error);
  rhi_set_shader(RHI_STAGE_VERTEX, vertex);
  rhi_set_shader(RHI_STAGE_PIXEL, pixel);
  RhiRasterDesc raster{};
  raster.fill = RHI_FILL_SOLID;
  raster.cull = RHI_CULL_NONE;
  raster.depth_clip = 1;
  auto *state = rhi_raster_state_create(&raster);
  check(state, "raster state");
  rhi_set_raster_state(state);
  rhi_set_topology(RHI_TOPOLOGY_TRIANGLES);
  rhi_draw(3, 0);
  std::vector<uint8_t> bytes(32 * 32 * 4);
  check(!rhi_image_readback(target, 0, bytes.data(), 32 * 4),
        "ordered GPU readback");
  for (size_t i = 0; i < bytes.size(); i += 4)
    check(bytes[i] == 255 && bytes[i + 1] == 64 && bytes[i + 2] == 0 &&
              bytes[i + 3] == 255,
          "draw pixels");
  rhi_set_render_target(nullptr, nullptr);
  rhi_raster_state_destroy(state);
  rhi_shader_destroy(vertex);
  rhi_shader_destroy(pixel);
  rhi_view_destroy(view);
  rhi_image_destroy(target);
  rhi_device_destroy();
  std::puts(
      "borrowed phobos D3D11: real hardware draw and ordered readback passed");
}
