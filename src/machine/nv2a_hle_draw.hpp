#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace xlle {
// Owned inputs to the separate HLE renderer. No software-transformed vertices,
// raster triangles, instruction histories or shadow execution cross this seam.
struct Nv2aHleSurface {
  uint32_t address = 0, width = 0, height = 0, pitch = 0, format = 0;
  bool depth = false, swizzled = false;
  std::vector<uint8_t> bytes;
};
struct Nv2aHleTexture {
  uint32_t address = 0, width = 0, height = 0, pitch = 0, format = 0;
  uint32_t depth = 1, dimensions = 2;
  uint32_t levels = 1, address_mode = 0, filter = 0, border = 0;
  bool present = false, linear = false, cubemap = false, resident = false;
  std::vector<uint8_t> bytes, palette;
};
struct Nv2aHleDraw {
  using Vertex = std::array<std::array<float, 4>, 16>;
  std::vector<Vertex> vertices;
  // Original guest streams, as used by NoRain's native presenter. Indexed
  // batches need no CPU attribute expansion; the GPU decodes these formats.
  std::vector<uint8_t> raw_vertices;
  std::array<std::array<int32_t, 4>, 16>
      vertex_layout{}; // byte offset/stride/type/count
  Vertex default_attributes{};
  uint32_t raw_vertex_count = 0;
  std::vector<uint32_t> indices;
  std::vector<uint32_t> state;
  std::vector<std::array<uint32_t, 4>> program;
  std::array<std::array<float, 4>, 192> constants{};
  uint32_t primitive = 0, transform_mode = 0;
  bool ambient_lighting = false, specular_alpha_from_vertex = false;
  std::array<float, 4> ambient_color{};
  Nv2aHleSurface color, depth;
  std::array<Nv2aHleTexture, 4> textures;
};
} // namespace xlle
