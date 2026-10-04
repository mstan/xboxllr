// phobos665/xboxrecomp f01b20ded38856abd3d37b561521cc7013363725, GPL-3.0.
// Extracted generator; optional debug color replacement removed.
#include "d3d8_combiners.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
enum {
    XTM_NONE = 0x00, XTM_PROJECT2D = 0x01, XTM_PROJECT3D = 0x02,
    XTM_CUBEMAP = 0x03, XTM_PASSTHRU = 0x04, XTM_CLIPPLANE = 0x05,
    XTM_BUMPENVMAP = 0x06, XTM_BUMPENVMAP_LUM = 0x07, XTM_BRDF = 0x08,
    XTM_DOT_ST = 0x09, XTM_DOT_ZW = 0x0A, XTM_DOT_RFLCT_DIFF = 0x0B,
    XTM_DOT_RFLCT_SPEC = 0x0C, XTM_DOT_STR_3D = 0x0D, XTM_DOT_STR_CUBE = 0x0E,
    XTM_DPNDNT_AR = 0x0F, XTM_DPNDNT_GB = 0x10, XTM_DOTPRODUCT = 0x11,
    XTM_DOT_RFLCT_SPEC_CONST = 0x12, XTM_LAST = 0x12
};

#define EMIT(fmt, ...) do { \
    int _n = snprintf(buf + off, bufsize - off, fmt, ##__VA_ARGS__); \
    if (_n < 0 || off + _n >= bufsize) return -1; \
    off += _n; \
} while (0)

/**
 * Get the HLSL variable name for a register in the NV2A register file.
 *
 * The register file is represented as local float4 variables in the
 * generated shader. This returns the name used in the HLSL code.
 */
static const char *reg_name(NV2ACombinerRegister reg)
{
    switch (reg) {
    case NV2A_REG_ZERO:     return "r_zero";
    case NV2A_REG_C0:       return "r_c0";
    case NV2A_REG_C1:       return "r_c1";
    case NV2A_REG_FOG:      return "r_fog";
    case NV2A_REG_V0:       return "r_v0";
    case NV2A_REG_V1:       return "r_v1";
    case NV2A_REG_T0:       return "r_t0";
    case NV2A_REG_T1:       return "r_t1";
    case NV2A_REG_T2:       return "r_t2";
    case NV2A_REG_T3:       return "r_t3";
    case NV2A_REG_R0:       return "r_r0";
    case NV2A_REG_R1:       return "r_r1";
    case NV2A_REG_EF_PROD:  return "r_ef";
    case NV2A_REG_V1R0_SUM: return "r_v1r0sum";
    default:                return "r_zero";
    }
}

/**
 * Emit HLSL expression for reading a combiner input.
 *
 * An input consists of:
 *   1. Register selection (which variable to read)
 *   2. Channel selection (full RGBA or alpha-replicated)
 *   3. Mapping function (how to transform the value)
 *
 * For alpha-replicate: .aaaa swizzle
 * For normal RGB read in RGB path: .rgb (or .rgba for alpha path)
 *
 * The mapping function is applied inline as an arithmetic expression.
 *
 * @param suffix  ".rgb" for RGB path, ".a" for alpha path (determines swizzle)
 */
static void emit_mapped_input(char *buf, int bufsize, int *off,
                               const NV2ACombinerInput *input,
                               const char *suffix, int c0_idx, int c1_idx)
{
    const char *rn;
    char swizzle[8];
    char base_expr[128];
    int n;

    rn = reg_name(input->reg);

    /* C0/C1 of the stage that holds them (its own, or stage 0's when the
     * stages share them); the final combiner (index < 0) has its own pair,
     * fc0 and fc1. */
    if (input->reg == NV2A_REG_C0) {
        if (c0_idx < 0)
            snprintf(base_expr, sizeof(base_expr), "fc0");
        else
            snprintf(base_expr, sizeof(base_expr), "c0[%d]", c0_idx);
    } else if (input->reg == NV2A_REG_C1) {
        if (c1_idx < 0)
            snprintf(base_expr, sizeof(base_expr), "fc1");
        else
            snprintf(base_expr, sizeof(base_expr), "c1[%d]", c1_idx);
    } else {
        snprintf(base_expr, sizeof(base_expr), "%s", rn);
    }

    /* Determine swizzle based on alpha replicate and target channel.
     *
     * The bit selects the channel, and what "not alpha" means depends on the
     * portion: in the RGB portion it is rgb, in the ALPHA portion it is the
     * register's BLUE component, not its alpha (NV_register_combiners, and
     * Cxbx-Reloaded XbPixelShader.cpp, which emits .b there). Reading alpha
     * for those inputs collapsed Burnout 2's alpha chains to zero, so its
     * car, HUD and fade -- every blended draw -- disappeared with the
     * combiners on. */
    if (input->alpha_rep) {
        /* Alpha replicate: use .aaa for RGB, .a for alpha */
        if (strcmp(suffix, ".a") == 0)
            snprintf(swizzle, sizeof(swizzle), ".a");
        else
            snprintf(swizzle, sizeof(swizzle), ".aaa");
    } else if (strcmp(suffix, ".a") == 0) {
        snprintf(swizzle, sizeof(swizzle), ".b");
    } else {
        snprintf(swizzle, sizeof(swizzle), "%s", suffix);
    }

    /* Build the full variable reference */
    char var_ref[160];
    snprintf(var_ref, sizeof(var_ref), "%s%s", base_expr, swizzle);

    /* Apply input mapping */
    switch (input->mapping) {
    case NV2A_MAP_UNSIGNED_IDENTITY:
        /* x - passthrough */
        n = snprintf(buf + *off, bufsize - *off, "max(%s, 0.0)", var_ref);
        break;
    case NV2A_MAP_UNSIGNED_INVERT:
        /* 1 - x, x clamped to [0,1] first */
        n = snprintf(buf + *off, bufsize - *off,
                     "(1.0 - saturate(%s))", var_ref);
        break;
    case NV2A_MAP_EXPAND_NORMAL:
        /* 2x - 1 */
        n = snprintf(buf + *off, bufsize - *off,
                     "(2.0 * max(%s, 0.0) - 1.0)", var_ref);
        break;
    case NV2A_MAP_EXPAND_NEGATE:
        /* 1 - 2x */
        n = snprintf(buf + *off, bufsize - *off,
                     "(1.0 - 2.0 * max(%s, 0.0))", var_ref);
        break;
    case NV2A_MAP_HALFBIAS_NORMAL:
        /* x - 0.5 */
        n = snprintf(buf + *off, bufsize - *off,
                     "(max(%s, 0.0) - 0.5)", var_ref);
        break;
    case NV2A_MAP_HALFBIAS_NEGATE:
        /* 0.5 - x */
        n = snprintf(buf + *off, bufsize - *off,
                     "(0.5 - max(%s, 0.0))", var_ref);
        break;
    case NV2A_MAP_SIGNED_IDENTITY:
        /* x (allow negative values) */
        n = snprintf(buf + *off, bufsize - *off, "%s", var_ref);
        break;
    case NV2A_MAP_SIGNED_NEGATE:
        /* -x */
        n = snprintf(buf + *off, bufsize - *off, "(-%s)", var_ref);
        break;
    default:
        n = snprintf(buf + *off, bufsize - *off, "%s", var_ref);
        break;
    }
    if (n > 0) *off += n;
}

/**
 * Emit HLSL expression for the output mapping (scale/bias).
 */
static const char *output_map_prefix(NV2AOutputMapping map)
{
    switch (map) {
    case NV2A_OUT_IDENTITY:         return "";
    case NV2A_OUT_BIAS:             return "(";
    case NV2A_OUT_SHIFTLEFT_1:      return "(";
    case NV2A_OUT_SHIFTLEFT_1_BIAS: return "((";
    case NV2A_OUT_SHIFTLEFT_2:      return "(";
    case NV2A_OUT_SHIFTLEFT_2_BIAS: return "((";
    case NV2A_OUT_SHIFTRIGHT_1:     return "(";
    case NV2A_OUT_SHIFTRIGHT_1_BIAS: return "((";
    default:                        return "";
    }
}

static const char *output_map_suffix(NV2AOutputMapping map)
{
    switch (map) {
    case NV2A_OUT_IDENTITY:         return "";
    case NV2A_OUT_BIAS:             return " - 0.5)";
    case NV2A_OUT_SHIFTLEFT_1:      return " * 2.0)";
    case NV2A_OUT_SHIFTLEFT_1_BIAS: return " - 0.5) * 2.0)";
    case NV2A_OUT_SHIFTLEFT_2:      return " * 4.0)";
    case NV2A_OUT_SHIFTLEFT_2_BIAS: return " - 0.5) * 4.0)";
    case NV2A_OUT_SHIFTRIGHT_1:     return " * 0.5)";
    case NV2A_OUT_SHIFTRIGHT_1_BIAS: return " - 0.5) * 0.5)";
    default:                        return "";
    }
}

/* A shadow-map stage (its texture is a depth format;
 * d3d8_combiners_prepare_draw sets state->shadow): r_t<i> is the compare,
 * whatever the stage's mode.
 * Projected, then four neighbouring texels compared and the results
 * filtered bilinearly: the hardware's soft edge. Load, so the comparison
 * sees stored depth, not a blend of it.
 *
 * Both texture paths call this -- the title's own modes
 * (emit_xbox_textures) as well as the plain sampler kinds -- so the
 * compare does not depend on how the stage's mode reached the host. The
 * caller declares r_t<i>. */
static int emit_shadow_compare(char *buf, int bufsize, int *poff, int i)
{
    int off = *poff;

    EMIT("    {\n");
    EMIT("        float4 q = input.tc%d;\n", i);
    EMIT("        float qw = abs(q.w) > 1e-20 ? q.w : 1.0;\n");
    EMIT("        float2 uv = q.xy / qw * tex_scale[%d].xy;\n", i);
    EMIT("        float ref = q.z / qw;\n");
    EMIT("        float m = shadow_max[%d];\n", i);
    EMIT("        float2 sz; tex%d.GetDimensions(sz.x, sz.y);\n", i);
    EMIT("        float2 p = uv * sz - 0.5;\n");
    EMIT("        float2 f = frac(p);\n");
    EMIT("        int2 c0 = (int2)floor(p), hi = (int2)sz - 1;\n");
    EMIT("        float s00 = shadow_test(tex%d.Load(int3(clamp(c0, 0, hi), 0)).r * m, ref);\n", i);
    EMIT("        float s10 = shadow_test(tex%d.Load(int3(clamp(c0 + int2(1, 0), 0, hi), 0)).r * m, ref);\n", i);
    EMIT("        float s01 = shadow_test(tex%d.Load(int3(clamp(c0 + int2(0, 1), 0, hi), 0)).r * m, ref);\n", i);
    EMIT("        float s11 = shadow_test(tex%d.Load(int3(clamp(c0 + int2(1, 1), 0, hi), 0)).r * m, ref);\n", i);
    EMIT("        float s = lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);\n");
    /* RECOMP_D3D8_SHADOW_VIEW: the draw shows one of these instead
     * of its colour -- 1 the stored depth, 2 the reference r/q, 3 and
     * 4 the texel position across and down, 5 the filtered test, 6
     * the reference unscaled and 7 its fraction (its range, when 2
     * reads black). */
    EMIT("        if (shadow_func.z == 1) shadow_dbg = tex%d.Load(int3(clamp(c0, 0, hi), 0)).r;\n", i);
    EMIT("        if (shadow_func.z == 2) shadow_dbg = saturate(ref / m);\n");
    EMIT("        if (shadow_func.z == 3) shadow_dbg = saturate(uv.x);\n");
    EMIT("        if (shadow_func.z == 4) shadow_dbg = saturate(uv.y);\n");
    EMIT("        if (shadow_func.z == 5) shadow_dbg = s;\n");
    EMIT("        if (shadow_func.z == 6) shadow_dbg = saturate(ref);\n");
    EMIT("        if (shadow_func.z == 7) shadow_dbg = frac(ref);\n");
    EMIT("        r_t%d = float4(s, s, s, s);\n", i);
    EMIT("    }\n");
    *poff = off;
    return off;
}

/* The four texture registers from the title's own texture modes, in stage
 * order (a dependent stage reads an earlier one). The donor's incomplete
 * bump, clip-plane and depth-output modes are rejected in this import;
 * they never return placeholder results. Hardware-invalid stage assignments
 * retain the donor's documented register initialization. */
static int emit_xbox_textures(const NV2ACombinerState *state, char *buf,
                              int bufsize, int *poff)
{
    int off = *poff, i;

    EMIT("    float dot1 = 0.0, dot2 = 0.0, dot3 = 0.0;\n");
    for (i = 0; i < NV2A_MAX_TEXTURES; i++) {
        int m = state->xmode[i];
        int in = state->input_tex[i];
        int dm = state->dot_map[i] <= 3 ? state->dot_map[i] : 0;
        int dm3 = state->dot_map[3] <= 3 ? state->dot_map[3] : 0;

        EMIT("    float4 r_t%d = float4(0, 0, 0, 0);\n", i);
        if (state->shadow[i]) {
            *poff = off;
            if (emit_shadow_compare(buf, bufsize, poff, i) < 0)
                return -1;
            off = *poff;
            continue;   /* no alpha_only fix-up: the result is the test */
        }
        switch (m) {
        case XTM_NONE:
            EMIT("    r_t%d = float4(0, 0, 0, 1);\n", i);
            break;
        case XTM_PROJECT2D:
            EMIT("    r_t%d = tex%d.Sample(samp%d, input.tc%d.xy / qdiv(input.tc%d.w)"
                 " * tex_scale[%d].xy);\n", i, i, i, i, i, i);
            break;
        case XTM_PROJECT3D:
            EMIT("    r_t%d = tex%d.Sample(samp%d, input.tc%d.xyz / qdiv(input.tc%d.w));\n",
                 i, i, i, i, i);
            break;
        case XTM_CUBEMAP:
            EMIT("    r_t%d = tex%d.Sample(samp%d, input.tc%d.xyz);\n", i, i, i, i);
            break;
        case XTM_PASSTHRU:
            EMIT("    r_t%d = saturate(input.tc%d);\n", i, i);
            break;
        case XTM_DOT_ST:
            if (i < 2)
                break;
            EMIT("    dot%d = dot(input.tc%d.xyz, dotmap%d(r_t%d));\n", i, i, dm, in);
            EMIT("    r_t%d = tex%d.Sample(samp%d, float2(dot%d, dot%d) * tex_scale[%d].xy);\n",
                 i, i, i, i - 1, i, i);
            break;
        case XTM_DOTPRODUCT:
            if (i < 1 || (m == XTM_DOTPRODUCT && i > 2) || (m == XTM_DOT_ZW && i < 2))
                break;
            EMIT("    dot%d = dot(input.tc%d.xyz, dotmap%d(r_t%d));\n", i, i, dm, in);
            break;
        case XTM_DOT_RFLCT_DIFF:
            /* Stage 2 only: the normal is the three dot products, the third
             * computed here with stage 3's mapping and source. */
            if (i != 2)
                break;
            EMIT("    dot2 = dot(input.tc2.xyz, dotmap%d(r_t%d));\n", dm, in);
            EMIT("    r_t2 = tex2.Sample(samp2, float3(dot1, dot2,"
                 " dot(input.tc3.xyz, dotmap%d(r_t%d))));\n", dm3, state->input_tex[3]);
            break;
        case XTM_DOT_RFLCT_SPEC:
            /* Stage 3 only: the eye vector is the three stages' q, and the
             * lookup is along it reflected about the normal. */
            if (i != 3)
                break;
            EMIT("    dot3 = dot(input.tc3.xyz, dotmap%d(r_t%d));\n", dm, in);
            EMIT("    {\n"
                 "        float3 n = float3(dot1, dot2, dot3);\n"
                 "        float3 e = float3(input.tc1.w, input.tc2.w, input.tc3.w);\n"
                 "        float nn = dot(n, n);\n"
                 "        float3 rv = 2.0 * n * dot(n, e) / (nn != 0.0 ? nn : 1.0) - e;\n"
                 "        r_t3 = tex3.Sample(samp3, rv);\n"
                 "    }\n");
            break;
        case XTM_DOT_STR_3D:
        case XTM_DOT_STR_CUBE:
            if (i != 3)
                break;
            EMIT("    dot3 = dot(input.tc3.xyz, dotmap%d(r_t%d));\n", dm, in);
            EMIT("    r_t3 = tex3.Sample(samp3, float3(dot1, dot2, dot3));\n");
            break;
        case XTM_DPNDNT_AR:
        case XTM_DPNDNT_GB:
            if (i < 1)
                break;
            EMIT("    r_t%d = tex%d.Sample(samp%d, r_t%d.%s * tex_scale[%d].xy);\n",
                 i, i, i, in, m == XTM_DPNDNT_AR ? "ar" : "gb", i);
            break;
        default:
            return -1; // No placeholder texture results for unimplemented modes.
        }
        if (state->tex_mode[i] != NV2A_TEXMODE_NONE)
            EMIT("    if (alpha_only[%d]) r_t%d.rgb = 1.0;\n", i, i);
    }
    *poff = off;
    return off;
}

int d3d8_combiners_generate_hlsl(const NV2ACombinerState *state,
                                 char *buf, int bufsize)
{
    int off = 0;
    int i;

    /* ---- Texture samplers ---- */
    for (i = 0; i < NV2A_MAX_TEXTURES; i++) {
        if (state->tex_mode[i] != NV2A_TEXMODE_NONE) {
            if (state->shadow[i]) {
                /* A depth texture: read as 2D whatever the stage mode, and
                 * compared rather than filtered (below). */
                EMIT("Texture2D    tex%d : register(t%d);\n", i, i);
            } else if (state->tex_mode[i] == NV2A_TEXMODE_CUBEMAP) {
                EMIT("TextureCube  tex%d : register(t%d);\n", i, i);
            } else if (state->tex_mode[i] == NV2A_TEXMODE_3D) {
                EMIT("Texture3D    tex%d : register(t%d);\n", i, i);
            } else {
                EMIT("Texture2D    tex%d : register(t%d);\n", i, i);
            }
            EMIT("SamplerState samp%d : register(s%d);\n", i, i);
        }
    }
    EMIT("\n");

    /* ---- Constant buffer ---- */
    EMIT("cbuffer CombinerCB : register(b0) {\n");
    EMIT("    float4 c0[8];\n");    /* Per-stage C0 */
    EMIT("    float4 c1[8];\n");    /* Per-stage C1 */
    EMIT("    float4 fc0;\n");      /* Final combiner C0 */
    EMIT("    float4 fc1;\n");      /* Final combiner C1 */
    EMIT("    float4 fog_color;\n");
    EMIT("    float  alpha_ref;\n");
    EMIT("    uint   alpha_func;\n");
    EMIT("    uint   alpha_test_enable;\n");
    EMIT("    uint   fog_enable;\n");
    EMIT("    uint4  alpha_only;\n");
    EMIT("    float4 tex_scale[4];\n");   /* texel -> normalised, linear textures */
    EMIT("    float4 shadow_max;\n");     /* per stage: the depth format's top */
    EMIT("    uint4  shadow_func;\n");    /* x: D3DCMPFUNC, y: compare reversed */
    EMIT("};\n\n");

    /* ---- Input structure ----
     *
     * The vertex and pixel stages are linked by register, not by name
     * alone: each compiler packs its own signature, and a pixel shader reads
     * whichever register its packing gave an input. So every vertex stage
     * (d3d8_shaders.c's fixed function, d3d8_vsh.c's programs) and every
     * pixel stage (this one and d3d8_shaders.c's) declares the same
     * interface: four float4 texture coordinates, the fog factor at
     * TEXCOORD4, then the view position at TEXCOORD5 (which this one does
     * not read). The coordinates here used to be float3 while the programs
     * wrote float4: the compiler packed `fog` into TEXCOORD3's spare w, and
     * under a vertex program every combiner read oT3.w -- 1.0 unless the
     * program wrote it -- as its fog factor. */
    EMIT("struct PS_IN {\n");
    EMIT("    float4 pos     : SV_POSITION;\n");
    EMIT("    float4 color0  : COLOR0;\n");
    EMIT("    float4 color1  : COLOR1;\n");
    /* All four components: q divides a projected 2D lookup and a shadow-map
     * stage's compare, and the reflection modes take the eye vector from the
     * three stages' w. */
    EMIT("    float4 tc0     : TEXCOORD0;\n");
    EMIT("    float4 tc1     : TEXCOORD1;\n");
    EMIT("    float4 tc2     : TEXCOORD2;\n");
    EMIT("    float4 tc3     : TEXCOORD3;\n");
    /* The fog register's alpha is the fog factor the vertex stage
     * interpolated, which both vertex paths write to TEXCOORD4
     * (d3d8_shaders.c for fixed function, d3d8_vsh.c for a program). */
    EMIT("    float  fog     : TEXCOORD4;\n");
    EMIT("};\n\n");

    /* ---- Shadow compare ---- */
    for (i = 0; i < NV2A_MAX_TEXTURES && !state->shadow[i]; i++)
        ;
    if (i < NV2A_MAX_TEXTURES) {
        /* The NV2A's shadow-map read: the texel, in the depth format's own
         * units, against r/q under D3DRS_SHADOWFUNC; 1 where the test
         * passes. 0 (unset) passes everywhere rather than shadowing all. */
        EMIT("float shadow_test(float texel, float ref) {\n");
        EMIT("    float a = shadow_func.y ? ref : texel;\n");
        EMIT("    float b = shadow_func.y ? texel : ref;\n");
        EMIT("    switch (shadow_func.x) {\n");
        EMIT("    case 1: return 0.0;\n");
        EMIT("    case 2: return a <  b ? 1.0 : 0.0;\n");
        EMIT("    case 3: return a == b ? 1.0 : 0.0;\n");
        EMIT("    case 4: return a <= b ? 1.0 : 0.0;\n");
        EMIT("    case 5: return a >  b ? 1.0 : 0.0;\n");
        EMIT("    case 6: return a != b ? 1.0 : 0.0;\n");
        EMIT("    case 7: return a >= b ? 1.0 : 0.0;\n");
        EMIT("    default: return 1.0;\n");
        EMIT("    }\n");
        EMIT("}\n\n");
    }

    /* The dot-product stages' input mappings, PS_DOTMAPPING_* (xemu psh.c
     * dotmap_*): 0..1 as is, and three ways of reading an unsigned byte as
     * -1..1. The HILO mappings (4-7) are read as 0..1. */
    if (state->xmode[0] != NV2A_XMODE_UNKNOWN) {
        EMIT("float qdiv(float q) { return q != 0.0 ? q : 1.0; }\n");
        EMIT("float3 dotmap0(float4 c) { return c.rgb; }\n");
        EMIT("float3 dotmap1(float4 c) { return (c.rgb * 255.0 - 128.0) / 127.0; }\n");
        EMIT("float3 dotmap2(float4 c) { float3 x = c.rgb * 255.0;\n"
             "    return x >= 128.0 ? (x - 255.5) / 127.5 : (x + 0.5) / 127.5; }\n");
        EMIT("float3 dotmap3(float4 c) { float3 x = c.rgb * 255.0;\n"
             "    return x >= 128.0 ? (x - 256.0) / 127.0 : x / 127.0; }\n\n");
    }

    /* ---- Main function ---- */
    EMIT("float4 main(PS_IN input) : SV_TARGET {\n");
    EMIT("    float shadow_dbg = 0.0;\n");

    /* Initialize register file */
    EMIT("    /* Register file initialization */\n");
    EMIT("    float4 r_zero = float4(0, 0, 0, 0);\n");
    EMIT("    float4 r_c0   = c0[0];\n");
    EMIT("    float4 r_c1   = c1[0];\n");
    /* The NV2A fog register is the fog colour in rgb and the interpolated fog
     * factor in alpha. Taking alpha from the fog colour instead made the
     * final combiner's usual lerp -- A = fog alpha, B = shaded colour,
     * C = fog colour -- constant across the frame, which washed the whole
     * image in fog colour or saturated it to white.
     *
     * The factor is the vertex stage's fog output whether or not fog is
     * enabled. It used to read 1 (no fog) with fog disabled, because taking
     * the interpolated value regardless blanked Burnout 2's menus to the fog
     * colour -- but under a vertex program the value interpolated then was
     * not the fog output at all (PS_IN above). With the stages linked, a
     * program that writes no fog delivers 1 (its default), and the
     * fixed-function stage writes 1 unless fog is on. TimeSplitters: Future
     * Perfect needs the real value: it turns fog off and blends by its own
     * factor, computed in its vertex programs (A = fog alpha, B = fog
     * colour, C = r0), so with 1 forced here its guns, hands and cave came
     * out the flat fog colour. */
    EMIT("    float4 r_fog  = float4(fog_color.rgb, saturate(input.fog));\n");

    /* Vertex colors: Xbox D3DCOLOR is BGRA in memory, the vertex shader
     * should have already swizzled to RGBA. */
    EMIT("    float4 r_v0   = input.color0;\n");
    EMIT("    float4 r_v1   = input.color1;\n");

    /* Texture samples */
    if (state->xmode[0] != NV2A_XMODE_UNKNOWN) {
        if (emit_xbox_textures(state, buf, bufsize, &off) < 0)
            return -1;
    } else for (i = 0; i < NV2A_MAX_TEXTURES; i++) {
        if (state->tex_mode[i] == NV2A_TEXMODE_NONE) {
            EMIT("    float4 r_t%d = float4(0, 0, 0, 0);\n", i);
        } else if (state->shadow[i]) {
            EMIT("    float4 r_t%d;\n", i);
            if (emit_shadow_compare(buf, bufsize, &off, i) < 0)
                return -1;
            continue;   /* no alpha_only fix-up: the result is the test */
        } else if (state->tex_mode[i] == NV2A_TEXMODE_CUBEMAP) {
            /* Cube map: use the full 3-component reflection/TCI vector */
            EMIT("    float4 r_t%d = tex%d.Sample(samp%d, input.tc%d.xyz);\n",
                 i, i, i, i);
        } else if (state->tex_mode[i] == NV2A_TEXMODE_3D) {
            /* 3D texture: use the full 3-component coordinate */
            EMIT("    float4 r_t%d = tex%d.Sample(samp%d, input.tc%d.xyz);\n",
                 i, i, i, i);
        } else {
            EMIT("    float4 r_t%d = tex%d.Sample(samp%d, input.tc%d.xy * tex_scale[%d].xy);\n",
                 i, i, i, i, i);
        }
        /* Preserve disabled stages and sampled alpha. */
        if (state->tex_mode[i] != NV2A_TEXMODE_NONE)
            EMIT("    if (alpha_only[%d]) r_t%d.rgb = 1.0;\n", i, i);
    }

    /* Temporary registers: r0 starts at zero with texture 0's alpha, or 1
     * when stage 0 samples nothing (xemu psh.c; upstream nv2a_combiner.c);
     * r1 at zero. */
    EMIT("    float4 r_r0 = float4(0, 0, 0, %s);\n",
         (state->xmode[0] != NV2A_XMODE_UNKNOWN ? state->xmode[0] != XTM_NONE
                                                : state->tex_mode[0] != NV2A_TEXMODE_NONE)
         ? "r_t0.a" : "1.0");
    EMIT("    float4 r_r1 = float4(0, 0, 0, 0);\n\n");

    /* ---- General combiner stages ----
     *
     * A stage as the NV2A computes it (xemu psh.c, add_stage_code and
     * get_combiner_output; upstream xboxrecomp src/kernel/nv2a_combiner.c):
     *  - both portions read all their inputs before either writes, so an
     *    alpha input sees the registers as the stage found them, not what
     *    its own RGB portion just wrote;
     *  - the products, and the sum or mux of the raw products, go through
     *    the output mapping and are clamped to -1..1, as main already did at
     *    the write (Outrun 2's road came out white without the clamp);
     *  - the mux returns CD when its bit of r0.a is set and AB otherwise
     *    (this had it the other way round), testing r0.a >= 0.5 with
     *    PS_COMBINERCOUNT_MUX_MSB and the byte's low bit without it;
     *  - a stage reads its own C0/C1 with PS_COMBINERCOUNT_UNIQUE_C0/C1 and
     *    stage 0's without;
     *  - AB_BLUE_TO_ALPHA / CD_BLUE_TO_ALPHA also write the product's blue
     *    to the destination's alpha (RGB portion only).
     * When PSCOMBINERCOUNT was never given (count_flags 0) the constants and
     * the mux bit stay as they were: per stage, and r0.a >= 0.5. */
    for (i = 0; i < state->num_stages; i++) {
        const NV2ACombinerInput *rgb_in  = state->stages[i].rgb_input;
        const NV2ACombinerInput *alpha_in = state->stages[i].alpha_input;
        const NV2ACombinerOutput *rgb_out = &state->stages[i].rgb_output;
        const NV2ACombinerOutput *alpha_out = &state->stages[i].alpha_output;
        int known = (state->count_flags & NV2A_COUNT_KNOWN) != 0;
        int c0i = (!known || (state->count_flags & 0x010u)) ? i : 0;
        int c1i = (!known || (state->count_flags & 0x100u)) ? i : 0;
        const char *omp, *oms;
        int k;
        static const char *const rn_rgb[4] = { "a_rgb", "b_rgb", "c_rgb", "d_rgb" };
        static const char *const rn_a[4] = { "a_a", "b_a", "c_a", "d_a" };

        EMIT("    /* ---- Stage %d ---- */\n", i);
        EMIT("    r_c0 = c0[%d];\n", c0i);
        EMIT("    r_c1 = c1[%d];\n", c1i);
        EMIT("    {\n");
        EMIT("        bool mux_cd = %s;\n", (!known || (state->count_flags & 0x001u))
             ? "r_r0.a >= 0.5"
             : "(((uint)(saturate(r_r0.a) * 255.0 + 0.5)) & 1u) != 0u");
        for (k = 0; k < 4; k++) {
            EMIT("        float3 %s = ", rn_rgb[k]);
            emit_mapped_input(buf, bufsize, &off, &rgb_in[k], ".rgb", c0i, c1i);
            EMIT(";\n");
        }
        for (k = 0; k < 4; k++) {
            EMIT("        float %s = ", rn_a[k]);
            emit_mapped_input(buf, bufsize, &off, &alpha_in[k], ".a", c0i, c1i);
            EMIT(";\n");
        }
        EMIT("        float3 ab_rgb = %s;\n",
             rgb_out->ab_dot ? "dot(a_rgb, b_rgb).xxx" : "a_rgb * b_rgb");
        EMIT("        float3 cd_rgb = %s;\n",
             rgb_out->cd_dot ? "dot(c_rgb, d_rgb).xxx" : "c_rgb * d_rgb");
        EMIT("        float3 sum_rgb = %s;\n",
             rgb_out->mux_sum ? "mux_cd ? cd_rgb : ab_rgb" : "ab_rgb + cd_rgb");
        EMIT("        float ab_a = a_a * b_a;\n");
        EMIT("        float cd_a = c_a * d_a;\n");
        EMIT("        float sum_a = %s;\n",
             alpha_out->mux_sum ? "mux_cd ? cd_a : ab_a" : "ab_a + cd_a");

        omp = output_map_prefix(rgb_out->output_map);
        oms = output_map_suffix(rgb_out->output_map);
        EMIT("        ab_rgb = clamp(%sab_rgb%s, -1.0, 1.0);\n", omp, oms);
        EMIT("        cd_rgb = clamp(%scd_rgb%s, -1.0, 1.0);\n", omp, oms);
        EMIT("        sum_rgb = clamp(%ssum_rgb%s, -1.0, 1.0);\n", omp, oms);
        omp = output_map_prefix(alpha_out->output_map);
        oms = output_map_suffix(alpha_out->output_map);
        EMIT("        ab_a = clamp(%sab_a%s, -1.0, 1.0);\n", omp, oms);
        EMIT("        cd_a = clamp(%scd_a%s, -1.0, 1.0);\n", omp, oms);
        EMIT("        sum_a = clamp(%ssum_a%s, -1.0, 1.0);\n", omp, oms);

        if (rgb_out->ab_dst != NV2A_REG_ZERO) {
            EMIT("        %s.rgb = ab_rgb;\n", reg_name(rgb_out->ab_dst));
            if (rgb_out->ab_blue_to_alpha)
                EMIT("        %s.a = ab_rgb.b;\n", reg_name(rgb_out->ab_dst));
        }
        if (rgb_out->cd_dst != NV2A_REG_ZERO) {
            EMIT("        %s.rgb = cd_rgb;\n", reg_name(rgb_out->cd_dst));
            if (rgb_out->cd_blue_to_alpha)
                EMIT("        %s.a = cd_rgb.b;\n", reg_name(rgb_out->cd_dst));
        }
        if (rgb_out->sum_dst != NV2A_REG_ZERO)
            EMIT("        %s.rgb = sum_rgb;\n", reg_name(rgb_out->sum_dst));
        if (alpha_out->ab_dst != NV2A_REG_ZERO)
            EMIT("        %s.a = ab_a;\n", reg_name(alpha_out->ab_dst));
        if (alpha_out->cd_dst != NV2A_REG_ZERO)
            EMIT("        %s.a = cd_a;\n", reg_name(alpha_out->cd_dst));
        if (alpha_out->sum_dst != NV2A_REG_ZERO)
            EMIT("        %s.a = sum_a;\n", reg_name(alpha_out->sum_dst));
        EMIT("    }\n\n");
    }

    /* ---- Final combiner ----
     *
     * The NV2A final combiner computes:
     *   result.rgb = D + lerp(C, B, A)
     *              = D + A*B + (1-A)*C
     *   result.a   = G.a
     *
     * Additionally, E*F is computed and made available as the EF_PROD
     * register, and V1+R0 is available as V1R0_SUM. These are computed
     * BEFORE the final combiner reads its inputs.
     */
    EMIT("    /* ---- Final Combiner ---- */\n");

    /* Compute specials: EF product and V1R0 sum */
    EMIT("    float4 r_ef = float4(0, 0, 0, 0);\n");
    EMIT("    float4 r_v1r0sum = float4(0, 0, 0, 0);\n");

    /* V1 + R0, colour only, with the final combiner settings: either
     * term complemented (COMPLEMENT_V1 0x40, COMPLEMENT_R0 0x20) and the sum
     * clamped only with CLAMP_SUM (0x80). This clamped it always and never
     * complemented. Then E * F, colour only, as upstream's executor has
     * both. */
    EMIT("    r_v1r0sum.rgb = %s(%s + %s);\n",
         (state->final_flags & 0x80u) ? "saturate" : "",
         (state->final_flags & 0x40u) ? "(1.0 - r_v1.rgb)" : "r_v1.rgb",
         (state->final_flags & 0x20u) ? "(1.0 - r_r0.rgb)" : "r_r0.rgb");
    EMIT("    r_ef.rgb = ");
    emit_mapped_input(buf, bufsize, &off, &state->final_input[4], ".rgb", -1, -1);
    EMIT(" * ");
    emit_mapped_input(buf, bufsize, &off, &state->final_input[5], ".rgb", -1, -1);
    EMIT(";\n\n");

    /* Final combiner: result.rgb = D + A*B + (1-A)*C */
    /* C0/C1 in the final combiner are its own constants (fc0, fc1) */
    {
        int fc_stage = -1;

        EMIT("    float4 result;\n");
        EMIT("    {\n");

        /* Read final combiner inputs A, B, C, D */
        EMIT("        float3 fc_a = ");
        emit_mapped_input(buf, bufsize, &off, &state->final_input[0], ".rgb", fc_stage, fc_stage);
        EMIT(";\n");
        EMIT("        float3 fc_b = ");
        emit_mapped_input(buf, bufsize, &off, &state->final_input[1], ".rgb", fc_stage, fc_stage);
        EMIT(";\n");
        EMIT("        float3 fc_c = ");
        emit_mapped_input(buf, bufsize, &off, &state->final_input[2], ".rgb", fc_stage, fc_stage);
        EMIT(";\n");
        EMIT("        float3 fc_d = ");
        emit_mapped_input(buf, bufsize, &off, &state->final_input[3], ".rgb", fc_stage, fc_stage);
        EMIT(";\n");

        /* result.rgb = D + lerp(C, B, A) = D + A*B + (1-A)*C */
        EMIT("        result.rgb = saturate(fc_d + fc_a * fc_b + (1.0 - fc_a) * fc_c);\n");

        /* result.a = G.a */
        EMIT("        result.a = ");
        emit_mapped_input(buf, bufsize, &off, &state->final_input[6], ".a", fc_stage, fc_stage);
        EMIT(";\n");

        EMIT("    }\n\n");
    }

    /* ---- Fog ---- */
    /* No fog after the final combiner. The NV2A has no fog stage of its own:
     * the XDK puts fog into the final combiner (A = fog alpha, B = r0,
     * C = fog colour), and that has already run. Blending again here fogged
     * every pixel twice. */

    /* ---- Alpha test ---- */
    EMIT("    /* Alpha test */\n");
    EMIT("    if (alpha_test_enable) {\n");
    EMIT("        bool alpha_pass = true;\n");
    EMIT("        if      (alpha_func == 1u) alpha_pass = false;\n");
    EMIT("        else if (alpha_func == 2u) alpha_pass = (result.a <  alpha_ref);\n");
    EMIT("        else if (alpha_func == 3u) alpha_pass = (result.a == alpha_ref);\n");
    EMIT("        else if (alpha_func == 4u) alpha_pass = (result.a <= alpha_ref);\n");
    EMIT("        else if (alpha_func == 5u) alpha_pass = (result.a >  alpha_ref);\n");
    EMIT("        else if (alpha_func == 6u) alpha_pass = (result.a != alpha_ref);\n");
    EMIT("        else if (alpha_func == 7u) alpha_pass = (result.a >= alpha_ref);\n");
    EMIT("        if (!alpha_pass) discard;\n");
    EMIT("    }\n\n");


    for (i = 0; i < NV2A_MAX_TEXTURES && !state->shadow[i]; i++)
        ;
    if (i < NV2A_MAX_TEXTURES)
        EMIT("    if (shadow_func.z) return float4(shadow_dbg, shadow_dbg, shadow_dbg, 1.0);\n");
    EMIT("    return result;\n");
    EMIT("}\n");

    return off;
}

#undef EMIT
