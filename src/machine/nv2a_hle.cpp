#include "machine/nv2a.hpp"
#include "machine/nv2a_hle_draw.hpp"
#include <algorithm>
#include <cstring>

namespace xlle {
bool Nv2a::submit_hle_vertex_batch() {
  auto fail = [&](const std::string &why) {
    fault_(("HLE renderer: " + why).c_str());
    return false;
  };
  if (!renderer_)
    return fail("renderer unavailable");
  const bool indexed = !vertex_indices_.empty(),
             inlined = !inline_vertices_.empty();
  const bool immediate = !immediate_vertices_.empty();
  if (!indexed && !inlined && !immediate)
    return true;
  if (unsigned(indexed) + unsigned(inlined) + unsigned(immediate) != 1)
    return fail("mixed vertex sources");
  Nv2aHleDraw p;
  p.primitive = primitive_mode_;
  p.state.assign(raster_state_.begin(), raster_state_.end());
  p.transform_mode = transform_execution_mode_;
  p.constants = transform_constants_;
  if (p.transform_mode == 2) {
    bool final = false;
    for (size_t i = transform_program_start_; i < transform_program_.size();
         ++i) {
      const auto &op = transform_program_[i];
      if (((op[1] >> 21) & 15) > 13)
        return fail("invalid vertex opcode");
      if ((op[3] & 0xf000) && !(op[3] & 0x800))
        return fail("vertex program writes persistent constants; unsupported");
      p.program.push_back(op);
      if (op[3] & 1) {
        final = true;
        break;
      }
    }
    if (!final)
      return fail("unterminated vertex program");
  } else if (p.transform_mode != 0) {
    return fail("unsupported transform mode " +
                std::to_string(p.transform_mode));
  } else if (fixed_skin_mode_ || fixed_fog_ || fixed_light_enable_mask_ ||
             std::any_of(fixed_texture_matrix_enable_.begin(),
                         fixed_texture_matrix_enable_.end(),
                         [](bool enabled) { return enabled; })) {
    return fail("fixed lighting/skinning/fog/texture matrix needs native "
                "implementation");
  }
  if (p.transform_mode == 0) {
    p.ambient_lighting = fixed_lighting_;
    p.specular_alpha_from_vertex = (fixed_light_control_ & 4) != 0;
    for (unsigned c = 0; c < 3; ++c)
      p.ambient_color[c] =
          fixed_scene_ambient_[c] * fixed_material_emission_[c] +
          fixed_scene_ambient_[c];
    p.ambient_color[3] = fixed_material_alpha_;
    for (const auto &stage : fixed_texgen_)
      for (unsigned mode : stage)
        if (mode)
          return fail("fixed texture generation unsupported");
  }
  const size_t count = indexed   ? vertex_indices_.size()
                       : inlined ? inline_vertices_.size()
                                 : immediate_vertices_.size();
  if (indexed) {
    const auto limits =
        std::minmax_element(vertex_indices_.begin(), vertex_indices_.end());
    const uint32_t first_index = *limits.first, last_index = *limits.second;
    if (uint64_t(last_index) - first_index + 1 > UINT32_MAX)
      return fail("vertex index span overflow");
    p.raw_vertex_count = last_index - first_index + 1;
    p.indices.reserve(count);
    for (uint32_t index : vertex_indices_)
      p.indices.push_back(index - first_index);
    struct Span {
      uint32_t start;
      uint64_t end;
      size_t offset = 0;
    };
    std::vector<Span> spans;
    std::array<uint32_t, 16> addresses{};
    for (unsigned a = 0; a < 16; ++a) {
      auto &attr = vertex_attributes_[a];
      p.default_attributes[a] = attr.value;
      if (!attr.count)
        continue;
      const uint64_t begin =
          uint64_t(attr.offset) + uint64_t(attr.stride) * first_index;
      const uint64_t end = uint64_t(attr.offset) +
                           uint64_t(attr.stride) * last_index +
                           uint64_t(attr.element_size) * attr.count;
      const uint32_t dma = attr.dma_b ? vertex_dma_b_ : vertex_dma_a_;
      uint32_t last_address;
      if (!(attr.dma_b ? vertex_dma_b_bound_ : vertex_dma_a_bound_) ||
          end <= begin || end > (uint64_t(1) << 32) ||
          end - begin > 64 * 1024 * 1024 ||
          !dma_object_address(dma, uint32_t(begin), addresses[a]) ||
          !dma_object_address(dma, uint32_t(end - 1), last_address) ||
          uint64_t(addresses[a]) + end - begin - 1 != last_address)
        return fail("raw vertex DMA range");
      spans.push_back({addresses[a], uint64_t(last_address) + 1});
      p.vertex_layout[a] = {0, int32_t(attr.stride), attr.format, attr.count};
    }
    std::sort(spans.begin(), spans.end(),
              [](const auto &a, const auto &b) { return a.start < b.start; });
    std::vector<Span> merged;
    for (const auto &span : spans) {
      if (!merged.empty() && span.start <= merged.back().end)
        merged.back().end = std::max(merged.back().end, span.end);
      else
        merged.push_back(span);
    }
    for (auto &span : merged) {
      const size_t size = size_t(span.end - span.start);
      span.offset = p.raw_vertices.size();
      if (span.offset + size > 64 * 1024 * 1024)
        return fail("raw vertex storage bound");
      if (!resolve_renderer_depth_overlap(span.start, size,
                                          "HLE vertex stream") ||
          !resolve_renderer_color_overlap(span.start, size,
                                          "HLE vertex stream"))
        return false;
      p.raw_vertices.resize((span.offset + size + 3) & ~size_t(3));
      const auto *direct =
          mem_direct_ ? mem_direct_(span.start, size) : nullptr;
      if (direct)
        std::memcpy(p.raw_vertices.data() + span.offset, direct, size);
      else if (!mem_read_ ||
               !mem_read_(span.start, p.raw_vertices.data() + span.offset,
                          size))
        return fail("raw vertex read");
    }
    // Even an all-constant stream needs a valid SSBO binding.
    if (p.raw_vertices.empty())
      p.raw_vertices.resize(4);
    for (unsigned a = 0; a < 16; ++a) {
      if (!vertex_attributes_[a].count)
        continue;
      for (const auto &span : merged)
        if (addresses[a] >= span.start && addresses[a] < span.end) {
          p.vertex_layout[a][0] =
              int32_t(span.offset + addresses[a] - span.start);
          break;
        }
      // Preserve current attribute state for subsequent immediate commands;
      // this is one decode per stream, rather than one decode per vertex.
      if (!fetch_vertex_attribute(a, vertex_indices_.back()))
        return false;
    }
  } else {
    p.indices.reserve(count);
    p.vertices.reserve(count);
    for (size_t n = 0; n < count; ++n) {
      const auto &inputs =
          inlined ? inline_vertices_[n] : immediate_vertices_[n];
      for (unsigned a = 0; a < 16; ++a)
        vertex_attributes_[a].value = inputs[a];
      p.indices.push_back(uint32_t(n));
      p.vertices.push_back(inputs);
    }
  }
  auto range = [&](uint32_t dma, bool bound, uint32_t offset, size_t size,
                   uint32_t &addr) {
    if (!bound || !size || uint64_t(offset) + size > uint64_t(UINT32_MAX) + 1)
      return false;
    uint32_t last = 0;
    return dma_object_address(dma, offset, addr) &&
           dma_object_address(dma, offset + uint32_t(size - 1), last) &&
           uint64_t(last) == uint64_t(addr) + size - 1;
  };
  auto snapshot_surface = [&](Nv2aHleSurface &s, bool depth) {
    s.depth = depth;
    s.format = surface_format_;
    s.swizzled = ((surface_format_ >> 8) & 15) == 2;
    const unsigned bpp = depth ? (((surface_format_ >> 4) & 15) == 1 ? 2 : 4)
                               : ((surface_format_ & 15) <= 3 ? 2 : 4);
    s.pitch = depth ? surface_pitch_ >> 16 : surface_pitch_ & 65535;
    s.height =
        (surface_clip_vertical_ & 65535) + (surface_clip_vertical_ >> 16);
    s.width = bpp ? s.pitch / bpp : 0;
    if (s.swizzled) {
      if ((surface_format_ >> 24) > 12 || ((surface_format_ >> 16) & 255) > 12)
        return false;
      s.width = 1u << ((surface_format_ >> 16) & 255);
      s.height = 1u << (surface_format_ >> 24);
      s.pitch = s.width * bpp;
    }
    const size_t size = size_t(s.pitch) * s.height;
    if (!s.width || !s.height || size > 64 * 1024 * 1024)
      return false;
    if (!range(depth ? zeta_dma_ : color_dma_,
               depth ? zeta_dma_bound_ : color_dma_bound_,
               depth ? surface_zeta_offset_ : surface_color_offset_, size,
               s.address))
      return false;
    if (depth && !s.address)
      return false;
    const int tile=ram_tile_at(s.address);
    if(depth && tile>=0 && (pgraph_tiles_.zcomp[unsigned(tile)]&0x80000000u) && bpp!=4)
      return false;
    if(ram_tile_overlap(s.address,size) &&
       (s.swizzled || tile<0 ||
        uint64_t(s.address)+size>uint64_t(pgraph_tiles_.tiles[unsigned(tile)][1])+1 ||
        (!depth && pgraph_tiles_.zcomp[unsigned(tile)])))return false;
    const bool resident =
        depth ? renderer_->has_resident_depth_surface(s.address, size, s.pitch,
                                                      s.height, s.format)
              : renderer_->has_resident_color_surface(s.address, size, s.pitch,
                                                      s.height, s.format);
    if (!resident) {
      if (!resolve_renderer_color_overlap(s.address, size, "HLE target") ||
          !resolve_renderer_depth_overlap(s.address, size, "HLE target"))
        return false;
      s.bytes.resize(size);
      if (!(ram_tiles_active() ? ram_tile_transfer(s.address,s.bytes.data(),size,false) : (mem_read_ && mem_read_(s.address,s.bytes.data(),size))))
        return false;
    }
    return true;
  };
  if (!snapshot_surface(p.color, false))
    return fail("invalid color target");
  if ((p.state[kRasterEnables] & ((1u << 3) | (1u << 5))) ||
      p.state[kRasterDepthMask]) {
    uint32_t zeta_base = 0;
    const unsigned depth_bpp = ((surface_format_ >> 4) & 15) == 1 ? 2 : 4;
    const unsigned clip_right =
        (surface_clip_horizontal_ & 65535) + (surface_clip_horizontal_ >> 16);
    const bool usable_pitch =
        (surface_pitch_ >> 16) >= uint64_t(clip_right) * depth_bpp;
    // The dashboard binds a 64-byte dummy zeta pitch during color-only
    // passes. It is not storage for the declared viewport. This is the
    // existing machine's unbound-depth contract, not a discarded draw.
    const bool bound_zeta =
        usable_pitch && zeta_dma_bound_ &&
        dma_object_address(zeta_dma_, surface_zeta_offset_, zeta_base);
    if (!bound_zeta || zeta_base == 0) {
      // Existing machine contract: an unbound zeta plane has no storage.
      // Render color normally; never write through the zero DMA address.
      p.depth = {};
      p.state[kRasterEnables] &= ~((1u << 3) | (1u << 5));
      p.state[kRasterDepthMask] = 0;
    } else if (!snapshot_surface(p.depth, true))
      return fail("invalid depth target or DMA read");
  }
  for (unsigned i = 0; i < 4; ++i) {
    const unsigned mode = (p.state[kRasterShaderStageProgram] >> (5 * i)) & 31;
    if (mode != 1 && mode != 2 && mode != 3)
      continue; // no texture fetch in NONE/PASSTHRU/CLIPPLANE
    auto &t = p.textures[i];
    const uint32_t format = p.state[kRasterTextureFormat0 + i];
    t.format = (format >> 8) & 255;
    t.linear = t.format == 0x12 || t.format == 0x1e || t.format == 0x10 ||
               t.format == 0x11 || t.format == 0x30 || t.format == 0x2e ||
               t.format == 0x35;
    t.cubemap = (format & 4) != 0;
    t.dimensions = (format >> 4) & 15;
    t.depth = t.dimensions == 3 ? 1u << (format >> 28) : 1;
    if (t.dimensions != 2 && t.dimensions != 3)
      return fail("unsupported texture dimensions");
    t.levels = std::max(1u, (format >> 16) & 15);
    t.width = t.linear ? p.state[kRasterTextureImageRect0 + i] >> 16
                       : 1u << ((format >> 20) & 15);
    t.height = t.linear ? p.state[kRasterTextureImageRect0 + i] & 65535
                        : 1u << ((format >> 24) & 15);
    const unsigned bpp =
        (t.format == 0x19 || t.format == 0x0b || t.format == 0) ? 1
        : (t.format == 4 || t.format == 5 || t.format == 2 || t.format == 3 ||
           t.format == 0x10 || t.format == 0x11 || t.format == 0x30 ||
           t.format == 0x2c || t.format == 0x35)
            ? 2
            : 4;
    t.pitch =
        t.linear ? p.state[kRasterTextureControl1_0 + i] >> 16 : t.width * bpp;
    if (!t.width || !t.height || t.width > 4096 || t.height > 4096)
      return fail("invalid texture dimensions");
    size_t span = 0;
    unsigned w = t.width, h = t.height, d = t.depth;
    for (unsigned level = 0; level < t.levels; ++level) {
      if (t.format == 0x0c || t.format == 0x0e || t.format == 0x0f)
        span +=
            size_t((w + 3) / 4) * ((h + 3) / 4) * (t.format == 0x0c ? 8 : 16);
      else
        span += t.linear ? size_t(t.pitch) * h * d : size_t(w) * h * d * bpp;
      w = std::max(1u, w / 2);
      h = std::max(1u, h / 2);
      d = std::max(1u, d / 2);
    }
    if (t.cubemap)
      span = ((span + 127) & ~size_t(127)) * 6;
    if (span > 64 * 1024 * 1024)
      return fail("texture span too large");
    const unsigned dma_sel = format & 3;
    if (dma_sel != 1 && dma_sel != 2)
      return fail("unbound texture DMA");
    if (!range(dma_sel == 1 ? texture_dma_a_ : texture_dma_b_,
               dma_sel == 1 ? texture_dma_a_bound_ : texture_dma_b_bound_,
               p.state[kRasterTextureOffset0 + i], span, t.address))
      return fail("texture DMA range");
    if(ram_tile_overlap(t.address,span)) {
      const int tile=ram_tile_at(t.address);
      if((format&~0x0000ff03u)!=0x00010028u ||
         (t.format!=0x12 && t.format!=0x1e) || tile<0 ||
         pgraph_tiles_.zcomp[unsigned(tile)] ||
         uint64_t(t.address)+span>uint64_t(pgraph_tiles_.tiles[unsigned(tile)][1])+1)
        return fail("tiled texture fetch layout/extent is not qualified");
    }
    t.address_mode = p.state[kRasterTextureAddress0 + i];
    t.filter = p.state[kRasterTextureFilter0 + i];
    t.border = p.state[kRasterTextureBorderColor0 + i];
    t.present = true;
    // Render-target textures stay on the GPU across dependent draws.
    const bool depth_texture =
        t.format == 0x30 || t.format == 0x2c || t.format == 0x2e;
    t.resident =
        t.linear && !depth_texture &&
        renderer_->has_resident_color_surface(
            t.address, size_t(t.pitch) * t.height, t.pitch, t.height, 0);
    if (depth_texture && t.linear)
      t.resident = renderer_->has_resident_depth_surface(
          t.address, size_t(t.pitch) * t.height, t.pitch, t.height, 0);
    if (!t.resident) {
      if (!resolve_renderer_depth_overlap(t.address, span, "HLE texture"))
        return false;
      if (!resolve_renderer_color_overlap(t.address, span, "HLE texture"))
        return false;
      t.bytes.resize(span);
      if (!(ram_tiles_active() ? ram_tile_transfer(t.address,t.bytes.data(),span,false) : (mem_read_ && mem_read_(t.address,t.bytes.data(),span))))
        return fail("texture read");
    }
    if (t.format == 0x0b) {
      const uint32_t pal = p.state[kRasterTexturePalette0 + i];
      const size_t bytes = 1024u >> ((pal >> 2) & 3);
      uint32_t addr;
      if (!range((pal & 1) ? texture_dma_b_ : texture_dma_a_,
                 (pal & 1) ? texture_dma_b_bound_ : texture_dma_a_bound_,
                 pal & ~63u, bytes, addr))
        return fail("palette DMA range");
      t.palette.resize(bytes);
      if (!mem_read_(addr, t.palette.data(), bytes))
        return fail("palette read");
    }
  }
  std::string error;
  if (!renderer_->submit_hle_draw(p, error))
    return fail(error);
  ++vertex_batches_;
  submitted_vertices_ += count;
  ++primitive_mode_counts_[primitive_mode_];
  return true;
}
} // namespace xlle
