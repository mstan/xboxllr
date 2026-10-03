#include "host/nv2a_xemu_psh.hpp"
#include "host/nv2a_opengl_renderer.hpp"
#include <cstdio>
#include <cstring>
#include <limits>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
using namespace xlle;

static Nv2aGeneratedShaderPacket make_packet(uint32_t address) {
    Nv2aGeneratedShaderPacket p;
    p.color_address = address;
    p.surface_width = p.surface_height = 8;
    p.surface_size = 8 * 8 * 4;
    p.surface_pitch = 0x00200020;
    p.surface_format = 0x128;
    p.xmax = p.ymax = 7;
    p.clip_max = 16777215.0f;
    p.needs_color_upload = true;
    p.color_input.resize(p.surface_size);
    p.state.stage_count = 1;
    p.state.needs_specular = true;
    p.state.final_enabled = true;
    p.state.final_cw0 = 0x0000000C;
    p.state.final_cw1 = 0x00001C80;
    p.state.color_icw[0] = 0x01200000; // C0 * 1 -> R0
    p.state.alpha_icw[0] = 0x11200000;
    p.state.color_ocw[0] = p.state.alpha_ocw[0] = 0xC0;
    p.stage_const0[0] = 0xFF123456;
    p.guest.valid = true;
    p.guest.combiner_control = 0x11101;
    p.guest.shade_mode = 0x1D01;
    p.guest.zminmax = 1;
    Nv2aGeneratedTriangle t;
    t.bx = t.cy = 128;
    t.bbox_xmax = t.bbox_ymax = 7;
    t.inverse_area = t.inv_area_recip = 1.0f / (128 * 128);
    t.inv_area_double = 1.0 / (128 * 128);
    t.constant_depth = true;
    for (auto& v : t.varying) {
        v[0] = {{1, 1, 1, 1}};
        v[2] = {{0.25f, 0.25f, 0, 1}};
    }
    p.triangles.push_back(t);
    p.fingerprint = nv2a_combiner_shader_fingerprint(p.state);
    return p;
}

static uint32_t read_pixel(Nv2aRenderer& r, Nv2aGeneratedShaderPacket& p) {
    p.fingerprint = nv2a_combiner_shader_fingerprint(p.state);
    CHECK(nv2a_xemu_psh_supported(p) == XemuPshReject::kNone);
    p.guest.shader_input = 0x00210000; // Inert routing bits must not reject independent modes.
    CHECK(nv2a_xemu_psh_supported(p) == XemuPshReject::kNone);
    CHECK(!nv2a_xemu_fragment_glsl(p).empty());
    CHECK(r.supports_generated_shader(p));
    CHECK(r.prepare_generated_shader(p));
    Nv2aGeneratedShaderResult result;
    std::string error;
    CHECK(r.submit_generated_shader(p, result, error));
    if (!error.empty()) std::printf("GL: %s\n", error.c_str());
    CHECK(result.triangles == 1 && result.draw_calls == 1);
    Nv2aResidentColorResolve resolved;
    CHECK(r.resolve_resident_color_surface(p.color_address, p.surface_size, resolved, error));
    CHECK(resolved.data.size() == p.surface_size);
    uint32_t value = 0;
    if (resolved.data.size() == p.surface_size) {
        std::memcpy(&value, resolved.data.data() + 36, 4); // (1,1), covered
        uint32_t outside = 1;
        std::memcpy(&outside, resolved.data.data() + 252, 4); // (7,7), uncovered
        CHECK(outside == 0);
    }
    return value;
}

int main() {
    auto p = make_packet(0x10000);
    CHECK(nv2a_xemu_psh_supported(p) == XemuPshReject::kNone);
    auto reject = [&](auto edit) {
        auto bad = p;
        edit(bad);
        CHECK(nv2a_xemu_psh_supported(bad) != XemuPshReject::kNone);
        CHECK(nv2a_xemu_fragment_glsl(bad).empty());
    };
    reject([](auto& b) { b.state.stage_count = 9; });
    reject([](auto& b) { b.state.color_icw[0] = 0x0F200000; }); // stage EF uninitialized in donor
    reject([](auto& b) { b.state.color_icw[0] = 0x03200000; }); // missing fog varying
    reject([](auto& b) { b.state.color_icw[0] = 0x08200000; }); // missing texture input
    reject([](auto& b) { b.state.color_icw[0] = 0x1C200000; }); // initial R0.a -> RGB
    reject([](auto& b) { b.state.alpha_icw[0] = 0x1C200000; }); // initial R0.a -> alpha
    reject([](auto& b) { b.state.color_ocw[0] |= 0x4D00; }); // implicit R0.a -> MUX
    reject([](auto& b) { b.state.alpha_ocw[0] = 0; }); // initial R0.a -> final G
    reject([](auto& b) { b.state.alpha_ocw[0] = 0; b.state.final_cw1 = 0x1C002000; }); // final E
    reject([](auto& b) { b.state.final_cw0 = b.state.final_cw1 = 0; });
    reject([](auto& b) { b.guest.shader_program = 8; }); // donor BRDF placeholder excluded
    reject([](auto& b) { b.guest.shader_input = 5; }); // donor fake hemisphere mapping excluded
    reject([](auto& b) { b.triangles[0].wide = true; });
    reject([](auto& b) { b.triangles[0].inv_w[0] = std::numeric_limits<float>::denorm_min(); });
    reject([](auto& b) { b.triangles[0].za = std::numeric_limits<float>::quiet_NaN(); });
    reject([](auto& b) { b.triangles[0].depth_bias = 1; });
    reject([](auto& b) { b.guest.control0 = 1 << 16; });

    std::string error;
    auto renderer = create_nv2a_opengl_renderer(error, true);
    if (!renderer) {
        std::printf("SKIP GPU: %s\n", error.c_str());
        return failures ? 1 : 77;
    }
    CHECK(std::strcmp(renderer->name(), "xemu-psh-opengl") == 0);
    CHECK(read_pixel(*renderer, p) == 0xFF123456);
    // Unread PROJECT2D state can retain a multi-mip texture from an earlier
    // draw. No texture bytes are required or sampled for this constant shader.
    p.color_address = 0x10400;
    p.guest.shader_program = 1;
    p.guest.texture_format[0] = 0x08890E2A;
    p.guest.texture_control[0] = 0x4003FFC0;
    p.guest.texture_filter[0] = 0x02062100;
    CHECK(read_pixel(*renderer, p) == 0xFF123456);
    reject([](auto& b) { b.guest.texture_control[0] |= 4; }); // alpha-kill still has effects
    reject([](auto& b) { b.guest.texture_control[0] |= 1; }); // color-key still has effects
    reject([](auto& b) { b.guest.shader_program = 2; }); // other modes may have side effects
    reject([](auto& b) { b.state.color_icw[0] = 0x08200000; }); // actually consumes T0
    // Independent perspective oracle: sample (1.5,1.5) gives barycentrics
    // (5/8,3/16,3/16), then reciprocal W (1,1/2,1/4).
    p = make_packet(0x10800);
    p.state.color_icw[0] = 0x04200000;
    auto& perspective = p.triangles[0];
    perspective.inv_w[1] = 0.5f;
    perspective.inv_w[2] = 0.25f;
    perspective.varying[0][0] = {{1, 0, 0, 1}};
    perspective.varying[1][0] = {{0, 1, 0, 1}};
    perspective.varying[2][0] = {{0, 0, 1, 1}};
    const uint32_t gradient = read_pixel(*renderer, p);
    CHECK(((gradient >> 16) & 255) >= 207 && ((gradient >> 16) & 255) <= 209);
    CHECK(((gradient >> 8) & 255) >= 30 && ((gradient >> 8) & 255) <= 32);
    CHECK((gradient & 255) >= 15 && (gradient & 255) <= 17);
    // Shared C0 with divergent per-stage values is a known reference gap;
    // admission refuses it. Equal shared values and unique C0 are real paths.
    p = make_packet(0x11000);
    p.state.stage_count = 2;
    p.state.color_icw[1] = p.state.color_icw[0];
    p.state.alpha_icw[1] = p.state.alpha_icw[0];
    p.state.color_ocw[1] = p.state.alpha_ocw[1] = 0xC0;
    p.stage_const0[1] = 0xFFABCDEF;
    p.guest.combiner_control = 2;
    CHECK(nv2a_xemu_psh_supported(p) == XemuPshReject::kCombiner);
    p.stage_const0[1] = p.stage_const0[0];
    CHECK(read_pixel(*renderer, p) == 0xFF123456);
    p.guest.combiner_control = 0x11102;
    p.stage_const0[1] = 0xFFABCDEF;
    p.color_address = 0x12000;
    CHECK(read_pixel(*renderer, p) == 0xFFABCDEF);

    // Real texture upload and donor textureProj, not captured output.
    p = make_packet(0x13000);
    p.state.color_icw[0] = 0x08200000;
    p.state.alpha_icw[0] = 0x18200000;
    p.state.textures[0].referenced = true;
    p.state.textures[0].mode = Nv2aCombinerShaderState::TexMode::kProject2d;
    p.guest.shader_program = 1;
    p.guest.texture_format[0] = 0x628;
    p.guest.texture_control[0] = 0x40000000;
    p.guest.texture_filter[0] = 0x01010000;
    auto& tex = p.textures[0];
    tex.present = true;
    tex.width = tex.height = 2;
    tex.content_id = 1;
    const uint32_t colors[] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF};
    tex.linear.resize(sizeof(colors));
    std::memcpy(tex.linear.data(), colors, sizeof(colors));
    CHECK(read_pixel(*renderer, p) == 0xFFFF0000);
    reject([](auto& b) { b.guest.texture_format[0] &= ~8u; });
    reject([](auto& b) { b.guest.texture_filter[0] = 0x02070000; });
    reject([](auto& b) { b.guest.texture_format[0] = 0x2C28; });
    reject([](auto& b) { b.triangles[0].varying[1][2][3] = -1; });
    // Reuse the exact uploaded image while changing filtering and coordinates.
    p.color_address = 0x13800;
    p.guest.texture_filter[0] = 0x02020000;
    for (auto& vertex : p.triangles[0].varying) vertex[2] = {{0.5f, 0.5f, 0, 1}};
    const uint32_t filtered = read_pixel(*renderer, p);
    for (unsigned shift : {0u, 8u, 16u})
        CHECK(((filtered >> shift) & 255) >= 127 && ((filtered >> shift) & 255) <= 129);
    p.color_address = 0x13C00;
    p.guest.texture_format[0] = 0x1228;
    p.state.textures[0].mode = Nv2aCombinerShaderState::TexMode::kLinear2d;
    for (auto& vertex : p.triangles[0].varying) vertex[2] = {{1, 1, 0, 1}};
    CHECK(read_pixel(*renderer, p) == filtered);

    // Already-decoded DXT packets use texel-space PROJECT2D just like the
    // floor's raw linear images. test_nv2a separately exercises real compressed
    // blocks through the device decoder (explicit DXT2/3 alpha and transparent
    // DXT1 selector). Here qualify that ARGB boundary on the actual donor GPU
    // path, including a reused texture identity with changed decoded contents.
    tex.width = tex.height = 4;
    tex.linear.resize(4 * 4 * 4);
    const auto fill_decoded = [&](uint32_t color) {
        for (size_t offset = 0; offset < tex.linear.size(); offset += 4)
            std::memcpy(tex.linear.data() + offset, &color, 4);
        ++tex.content_id;
    };
    p.guest.texture_format[0] = 0x0E28;
    p.guest.texture_filter[0] = 0x01010000;
    p.color_address = 0x17000;
    fill_decoded(0xAAFFFFFF); // 4-bit alpha A expanded by the device decoder.
    for (auto& vertex : p.triangles[0].varying) vertex[2] = {{2.5f, 2.5f, 0, 1}};
    CHECK(read_pixel(*renderer, p) == 0xAAFFFFFF);
    // Decoded source mutation must replace the previous uploaded texture.
    fill_decoded(0x55336699);
    p.color_address = 0x17800;
    CHECK(read_pixel(*renderer, p) == 0x55336699);
    p.guest.texture_filter[0] = 0x02020000;
    fill_decoded(0);
    const uint32_t white = 0xFFFFFFFF;
    std::memcpy(tex.linear.data(), &white, 4);
    p.color_address = 0x18000;
    for (auto& vertex : p.triangles[0].varying) vertex[2] = {{1, 1, 0, 1}};
    const uint32_t dxt_filtered = read_pixel(*renderer, p);
    for (unsigned shift : {0u, 8u, 16u, 24u})
        CHECK(((dxt_filtered >> shift) & 255) >= 63 &&
              ((dxt_filtered >> shift) & 255) <= 65);
    // Force MINIFICATION (four texels/pixel), with nearest MAG so a test
    // accidentally exercising magnification cannot pass. At pixel (1,1),
    // texel-space (u,v) = (1,1): one white and three zero taps yield 1/4.
    p.guest.texture_format[0] = 0x010E28; // exactly one declared mip level
    p.triangles[0].varying[0][2] = {{-5, -5, 0, 1}};
    p.triangles[0].varying[1][2] = {{27, -5, 0, 1}};
    p.triangles[0].varying[2][2] = {{-5, 27, 0, 1}};
    for (unsigned min_filter : {4u, 6u}) {
        p.guest.texture_filter[0] = 0x01000000 | (min_filter << 16);
        p.color_address = 0x19000 + min_filter * 0x100;
        const uint32_t minified = read_pixel(*renderer, p);
        for (unsigned shift : {0u, 8u, 16u, 24u})
            CHECK(((minified >> shift) & 255) >= 63 &&
                  ((minified >> shift) & 255) <= 65);
    }
    reject([](auto& b) { b.guest.texture_format[0] &= ~0x000F0000u; });
    reject([](auto& b) { b.guest.texture_format[0] += 0x00010000u; });
    reject([](auto& b) { b.guest.texture_filter[0] = 0x01070000; }); // convolution
    // BOX mip filters stay outside this new bilinear-only qualification.
    reject([](auto& b) { b.guest.texture_filter[0] = 0x01030000; });
    reject([](auto& b) { b.guest.texture_filter[0] = 0x01050000; });
    p.guest.texture_format[0] = 0x0C28;
    p.guest.texture_filter[0] = 0x01010000;
    p.color_address = 0x18800;
    fill_decoded(0); // DXT1's transparent selector, including RGB = zero.
    CHECK(read_pixel(*renderer, p) == 0);
    reject([](auto& b) { b.state.textures[0].mode = Nv2aCombinerShaderState::TexMode::kProject2d; });
    reject([](auto& b) { b.guest.shader_program = 1 | (1 << 5); });
    reject([](auto& b) { b.guest.texture_format[0] = 0x0F28; }); // DXT5 decoder not qualified here.

    p = make_packet(0x14000);
    p.state.color_format_x8 = true;
    p.state.blend = true;
    p.state.blend_src = p.blend_src = 0x302;
    p.state.blend_dst = p.blend_dst = 0x303;
    p.stage_const0[0] = 0x80FFFFFF;
    const uint32_t blended = read_pixel(*renderer, p);
    CHECK((blended >> 24) == 255);
    for (unsigned shift : {0u, 8u, 16u}) CHECK(((blended >> shift) & 255) >= 127 && ((blended >> shift) & 255) <= 129);

    p = make_packet(0x15000);
    p.state.alpha_test = true;
    p.state.alpha_func = 0x206; // GEQUAL
    p.alpha_ref = 129;
    p.stage_const0[0] = 0x80FFFFFF;
    CHECK(read_pixel(*renderer, p) == 0); // alpha128 is discarded
    // A declined donor draw must still execute the real reference shader.
    p = make_packet(0x16000);
    p.triangles[0].wide = true;
    CHECK(nv2a_xemu_psh_supported(p) == XemuPshReject::kGeometry);
    CHECK(renderer->supports_generated_shader(p));
    CHECK(renderer->prepare_generated_shader(p));
    Nv2aGeneratedShaderResult fallback_result;
    CHECK(renderer->submit_generated_shader(p, fallback_result, error));
    Nv2aResidentColorResolve fallback_image;
    CHECK(renderer->resolve_resident_color_surface(p.color_address, p.surface_size, fallback_image, error));
    uint32_t fallback_pixel = 0;
    if (fallback_image.data.size() == p.surface_size)
        std::memcpy(&fallback_pixel, fallback_image.data.data() + 36, 4);
    CHECK(fallback_pixel == 0xFF123456);

    std::printf("xemu PSH contract: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
