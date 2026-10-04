// phobos665/xboxrecomp f01b20ded38856abd3d37b561521cc7013363725, GPL-3.0.
// Extracted HLSL generator; title-specific aspect-ratio tracking removed.
#include "nv2a_vsh.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
static uint64_t g_emit_const_read[3];
static int g_emit_const_indexed, g_emit_pos_input;
/* String buffer helper */
typedef struct {
    char *buf;
    int   pos;
    int   size;
} StrBuf;

static void sb_init(StrBuf *sb, char *buf, int size)
{
    sb->buf  = buf;
    sb->pos  = 0;
    sb->size = size;
    if (size > 0) buf[0] = '\0';
}

static void sb_append(StrBuf *sb, const char *fmt, ...)
{
    va_list ap;
    int remaining;
    if (sb->pos >= sb->size - 1) return;
    remaining = sb->size - sb->pos;
    va_start(ap, fmt);
    int n = vsnprintf(sb->buf + sb->pos, remaining, fmt, ap);
    va_end(ap);
    if (n > 0 && n < remaining)
        sb->pos += n;
    else if (n >= remaining)
        sb->pos = sb->size - 1;
}

/* Component name table */
static const char g_comp_names[] = "xyzw";

/**
 * Emit a swizzle suffix.
 * If the swizzle is identity (.xyzw), emit nothing (saves readability).
 */
static void emit_swizzle(StrBuf *sb, const NV2AVshSwizzle *swz)
{
    /* Check for identity swizzle */
    if (swz->x == 0 && swz->y == 1 && swz->z == 2 && swz->w == 3)
        return;

    sb_append(sb, ".%c%c%c%c",
              g_comp_names[swz->x & 3],
              g_comp_names[swz->y & 3],
              g_comp_names[swz->z & 3],
              g_comp_names[swz->w & 3]);
}

/**
 * Emit a scalar swizzle for ILU ops that replicate a single component.
 * Uses .x/.y/.z/.w for the selected component.
 */
static void emit_scalar_swizzle(StrBuf *sb, const NV2AVshSwizzle *swz)
{
    /* ILU operations use only one component; the swizzle X field selects it */
    sb_append(sb, ".%c", g_comp_names[swz->x & 3]);
}

/**
 * Emit a source operand reference.
 *
 * Handles register bank selection, swizzle, negate, and relative addressing.
 */
static void emit_source(StrBuf *sb, const NV2AVshSrcOperand *src, int scalar)
{
    if (src->negate)
        sb_append(sb, "(-");

    switch (src->reg_type) {
    case NV2A_VSH_REG_TEMP:
        /* R12 is the oPos alias. R13-R15 do not exist: a read of one is
         * zero, as in the CPU interpreter, instead of an undeclared name that
         * fails the whole shader's compile. */
        if (src->reg_index >= 0 && src->reg_index <= 12)
            sb_append(sb, "R%d", src->reg_index);
        else
            sb_append(sb, "float4(0,0,0,0)");
        break;
    case NV2A_VSH_REG_INPUT:
        sb_append(sb, "v%d", src->reg_index);
        break;
    case NV2A_VSH_REG_CONST:
        if (src->rel_addr) {
            sb_append(sb, "c[a0 + %d]", src->reg_index);
            g_emit_const_indexed = 1;
        } else {
            sb_append(sb, "c[%d]", src->reg_index);
            if (src->reg_index >= 0 && src->reg_index < NV2A_VS_MAX_CONSTANTS)
                g_emit_const_read[src->reg_index >> 6] |= 1ull << (src->reg_index & 63);
            /* A program that reads the register holding the first column of
             * the title's projection is transforming geometry through it,
             * which is what makes it 3D. One that never does is drawing in
             * screen coordinates it worked out itself -- the HUD, menus,
             * a full-screen quad -- and is not widened by Hor+, so it needs
             * the compensating squeeze instead. Relative addressing is not
             * counted: an indexed read is a bone or a light, never this. */

        }
        break;
    default:
        sb_append(sb, "float4(0,0,0,0)");
        break;
    }

    if (scalar)
        emit_scalar_swizzle(sb, &src->swizzle);
    else
        emit_swizzle(sb, &src->swizzle);

    if (src->negate)
        sb_append(sb, ")");
}

/**
 * Emit a write mask suffix (.xyzw subset).
 * The mask is encoded as: bit3=x, bit2=y, bit1=z, bit0=w.
 */
static void emit_write_mask(StrBuf *sb, uint8_t mask)
{
    if (mask == 0xF) return; /* Full write, no mask needed */

    sb_append(sb, ".");
    if (mask & 0x8) sb_append(sb, "x");
    if (mask & 0x4) sb_append(sb, "y");
    if (mask & 0x2) sb_append(sb, "z");
    if (mask & 0x1) sb_append(sb, "w");
}

/**
 * Map NV2A output register enum to an HLSL variable name.
 */
static const char *output_reg_name(NV2AVshOutputReg reg)
{
    switch (reg) {
    case NV2A_VSH_OUT_POS:  return "oPos";
    case NV2A_VSH_OUT_D0:   return "oD0";
    case NV2A_VSH_OUT_D1:   return "oD1";
    case NV2A_VSH_OUT_FOG:  return "oFog";
    case NV2A_VSH_OUT_PTS:  return "oPts";
    case NV2A_VSH_OUT_B0:   return "oB0";
    case NV2A_VSH_OUT_B1:   return "oB1";
    case NV2A_VSH_OUT_T0:   return "oT0";
    case NV2A_VSH_OUT_T1:   return "oT1";
    case NV2A_VSH_OUT_T2:   return "oT2";
    case NV2A_VSH_OUT_T3:   return "oT3";
    default:                return NULL;
    }
}

/**
 * Emit a destination assignment (temp and/or output register write).
 *
 * The NV2A can write to both a temp register and an output register
 * simultaneously from the same operation. We emit two assignments
 * when both are active.
 *
 * @param prefix  The destination: temp register name
 * @param dst     The destination operand
 * @param rhs     The HLSL expression to assign (right-hand side)
 */
static void emit_dest_assign(StrBuf *sb, const NV2AVshDstOperand *dst,
                              const char *rhs)
{
    uint8_t omask = nv2a_vsh_output_write_mask(dst);

    /* rhs is a local computed once for the slot, so the order of these two
     * writes does not change the value. It did while rhs was the expression
     * itself: the temp write came first and the output then re-evaluated the
     * expression against the new temp. */
    if (dst->output_reg != NV2A_VSH_OUT_NONE && omask != 0) {
        const char *name = output_reg_name(dst->output_reg);
        if (name) {
            sb_append(sb, "        %s", name);
            emit_write_mask(sb, omask);
            sb_append(sb, " = (%s)", rhs);
            emit_write_mask(sb, omask);
            sb_append(sb, ";\n");
        }
    }

    /* R13-R15 do not exist; the interpreter ignores writes to them too. */
    if (dst->temp_reg >= 0 && dst->temp_reg <= 12 && dst->write_mask != 0) {
        sb_append(sb, "        R%d", dst->temp_reg);
        emit_write_mask(sb, dst->write_mask);
        sb_append(sb, " = (%s)", rhs);
        emit_write_mask(sb, dst->write_mask);
        sb_append(sb, ";\n");
    }
}

/**
 * Emit the HLSL for one MAC operation.
 */
static void emit_mac_op(StrBuf *sb, const NV2AVshInstruction *inst)
{
    StrBuf expr;
    char expr_buf[512];
    sb_init(&expr, expr_buf, sizeof(expr_buf));

    switch (inst->mac_op) {
    case NV2A_VSH_MAC_NOP:
        return;

    case NV2A_VSH_MAC_MOV:
        /* dst = A */
        emit_source(&expr, &inst->mac_src[0], 0);
        break;

    case NV2A_VSH_MAC_MUL:
        /* dst = A * B */
        sb_append(&expr, "(");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, " * ");
        emit_source(&expr, &inst->mac_src[1], 0);
        sb_append(&expr, ")");
        break;

    case NV2A_VSH_MAC_ADD:
        /* dst = A + C */
        sb_append(&expr, "(");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, " + ");
        emit_source(&expr, &inst->mac_src[2], 0);
        sb_append(&expr, ")");
        break;

    case NV2A_VSH_MAC_MAD:
        /* dst = A * B + C */
        sb_append(&expr, "(");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, " * ");
        emit_source(&expr, &inst->mac_src[1], 0);
        sb_append(&expr, " + ");
        emit_source(&expr, &inst->mac_src[2], 0);
        sb_append(&expr, ")");
        break;

    case NV2A_VSH_MAC_DP3:
        /* dst.xyzw = dot(A.xyz, B.xyz) replicated */
        sb_append(&expr, "dot(");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, ".xyz, ");
        emit_source(&expr, &inst->mac_src[1], 0);
        sb_append(&expr, ".xyz).xxxx");
        break;

    case NV2A_VSH_MAC_DPH:
        /* dst = dot(float4(A.xyz, 1.0), B) */
        sb_append(&expr, "dot(float4(");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, ".xyz, 1.0), ");
        emit_source(&expr, &inst->mac_src[1], 0);
        sb_append(&expr, ").xxxx");
        break;

    case NV2A_VSH_MAC_DP4:
        /* dst.xyzw = dot(A, B) replicated */
        sb_append(&expr, "dot(");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, ", ");
        emit_source(&expr, &inst->mac_src[1], 0);
        sb_append(&expr, ").xxxx");
        break;

    case NV2A_VSH_MAC_DST:
        /* dst = float4(1.0, A.y * B.y, A.z, B.w) */
        sb_append(&expr, "float4(1.0, ");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, ".y * ");
        emit_source(&expr, &inst->mac_src[1], 0);
        sb_append(&expr, ".y, ");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, ".z, ");
        emit_source(&expr, &inst->mac_src[1], 0);
        sb_append(&expr, ".w)");
        break;

    case NV2A_VSH_MAC_MIN:
        sb_append(&expr, "min(");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, ", ");
        emit_source(&expr, &inst->mac_src[1], 0);
        sb_append(&expr, ")");
        break;

    case NV2A_VSH_MAC_MAX:
        sb_append(&expr, "max(");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, ", ");
        emit_source(&expr, &inst->mac_src[1], 0);
        sb_append(&expr, ")");
        break;

    case NV2A_VSH_MAC_SLT:
        /* dst = (A < B) ? 1.0 : 0.0
         * SLT is the complement of SGE: slt(a,b) = 1 - step(b, a)
         * Equivalent to: step(a, b) where a < b yields 1
         * Using explicit form for clarity: */
        sb_append(&expr, "(1.0 - step(");
        emit_source(&expr, &inst->mac_src[1], 0);
        sb_append(&expr, ", ");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, "))");
        break;

    case NV2A_VSH_MAC_SGE:
        /* dst = (A >= B) ? 1.0 : 0.0
         * step(edge, x) returns 1 if x >= edge, 0 otherwise */
        sb_append(&expr, "step(");
        emit_source(&expr, &inst->mac_src[1], 0);
        sb_append(&expr, ", ");
        emit_source(&expr, &inst->mac_src[0], 0);
        sb_append(&expr, ")");
        break;

    case NV2A_VSH_MAC_ARL:
        /* a0 = floor(A.x). Held in a local and stored after the ILU has read
         * its source, like every other write in the slot. */
        sb_append(sb, "        int _a0 = (int)floor(");
        emit_source(sb, &inst->mac_src[0], 0);
        sb_append(sb, ".x);\n");
        return;

    default:
        return;
    }

    sb_append(sb, "        float4 _mac = %s;\n", expr_buf);
}

/**
 * Emit the HLSL for one ILU operation.
 */
static void emit_ilu_op(StrBuf *sb, const NV2AVshInstruction *inst)
{
    StrBuf expr;
    char expr_buf[512];
    sb_init(&expr, expr_buf, sizeof(expr_buf));

    switch (inst->ilu_op) {
    case NV2A_VSH_ILU_NOP:
        return;

    case NV2A_VSH_ILU_MOV:
        /* dst = C */
        emit_source(&expr, &inst->ilu_src, 0);
        break;

    case NV2A_VSH_ILU_RCP:
        /* dst = (1.0 / C.x).xxxx */
        sb_append(&expr, "(1.0 / ");
        emit_source(&expr, &inst->ilu_src, 1);
        sb_append(&expr, ").xxxx");
        break;

    case NV2A_VSH_ILU_RCC:
        /* dst = sign(1/C.x) * clamp(|1/C.x|, 5.42101e-20, 1.8446744e+19),
         * replicated -- the same asymmetric clamp nv2a_vsh_execute() applies.
         * The sign is taken as ">= 0", not sign(): at C.x = +/-inf, 1/x is a
         * signed zero, which sign() makes 0 and the interpreter treats as
         * positive. 1/0 is +/-inf and clamps to the upper bound. */
        sb_append(&expr, "(((1.0 / ");
        emit_source(&expr, &inst->ilu_src, 1);
        sb_append(&expr, ") >= 0.0 ? 1.0 : -1.0) * clamp(abs(1.0 / ");
        emit_source(&expr, &inst->ilu_src, 1);
        sb_append(&expr, "), 5.42101e-20, 1.8446744e+19)).xxxx");
        break;

    case NV2A_VSH_ILU_RSQ:
        /* dst = (1.0 / sqrt(abs(C.x))).xxxx */
        sb_append(&expr, "rsqrt(abs(");
        emit_source(&expr, &inst->ilu_src, 1);
        sb_append(&expr, ")).xxxx");
        break;

    case NV2A_VSH_ILU_EXP:
        /* dst = exp2(C.x).xxxx */
        sb_append(&expr, "exp2(");
        emit_source(&expr, &inst->ilu_src, 1);
        sb_append(&expr, ").xxxx");
        break;

    case NV2A_VSH_ILU_LOG: {
        /* dst = log2(abs(C.x)).xxxx
         * Guard against log2(0) which is -inf on NV2A -> clamp to large negative */
        sb_append(&expr, "log2(max(abs(");
        emit_source(&expr, &inst->ilu_src, 1);
        sb_append(&expr, "), 1.175494e-38)).xxxx");
        break;
    }

    case NV2A_VSH_ILU_LIT: {
        /* NV2A LIT instruction:
         *   dst.x = 1.0
         *   dst.y = max(src.x, 0.0)
         *   dst.z = (src.x > 0) ? pow(max(src.y, 0), clamp(src.w, -128, 128)) : 0
         *   dst.w = 1.0
         *
         * We emit a helper call. The lit() HLSL intrinsic has similar but
         * not identical semantics, so we use an inline expansion. */
        sb_append(&expr, "float4(1.0, max(");
        emit_source(&expr, &inst->ilu_src, 0);
        sb_append(&expr, ".x, 0.0), (");
        emit_source(&expr, &inst->ilu_src, 0);
        sb_append(&expr, ".x > 0.0) ? exp2(clamp(");
        emit_source(&expr, &inst->ilu_src, 0);
        sb_append(&expr, ".w, -128.0, 128.0) * log2(max(");
        emit_source(&expr, &inst->ilu_src, 0);
        sb_append(&expr, ".y, 0.0) + 1e-30)) : 0.0, 1.0)");
        break;
    }

    default:
        return;
    }

    sb_append(sb, "        float4 _ilu = %s;\n", expr_buf);
}

/**
 * Map NV2A input register index to a D3D11 input semantic.
 *
 * Xbox NV2A vertex shader input registers map to vertex attributes:
 *   v0  = Position
 *   v1  = Blend weight
 *   v2  = Normal
 *   v3  = Diffuse color
 *   v4  = Specular color
 *   v5  = Fog coordinate
 *   v6  = Point size / back diffuse
 *   v7  = Back specular
 *   v8  = Texture coord 0
 *   v9  = Texture coord 1
 *   v10 = Texture coord 2
 *   v11 = Texture coord 3
 *   v12-v15 = Additional attributes
 *
 * We use generic ATTR semantics so the input layout can match any
 * vertex buffer format at bind time.
 */
static const char *input_semantic_name(int reg_index)
{
    /* All inputs use the generic TEXCOORD semantic with unique indices
     * to avoid mismatches. The input layout will map them correctly. */
    (void)reg_index;
    return "ATTR";
}

int d3d8_vsh_generate_hlsl(const NV2AVshProgram *program,
                            char *buf, int bufsize)
{
    StrBuf sb;
    int i;
    uint16_t inputs = program->inputs_read;

    sb_init(&sb, buf, bufsize);

    /* Constant buffer: 192 float4 constants */
    sb_append(&sb,
        "/* Auto-generated NV2A vertex shader */\n"
        "\n"
        "cbuffer VSH_Constants : register(b1) {\n"
        "    float4 c[%d];\n"
        "};\n"
        "\n"
        "cbuffer VSH_Screenspace : register(b2) {\n"
        "    float4 xboxScreenspaceScale;\n"
        "    float4 xboxScreenspaceOffset;\n"
        "    float4 xboxScreenspaceEnable;\n"
        "};\n"
        "\n"
        "cbuffer VSH_VertexData : register(b3) {\n"
        "    float4 xboxVertexData[%d];\n"
        "    float4 xboxVertexFed[4];\n"
        "};\n"
        "\n", NV2A_VS_MAX_CONSTANTS, NV2A_VS_MAX_INPUTS);

    /* Input structure - only declare used inputs */
    sb_append(&sb, "struct VS_IN {\n");
    for (i = 0; i < NV2A_VS_MAX_INPUTS; i++) {
        if (inputs & (1u << i)) {
            sb_append(&sb, "    float4 v%d : ATTR%d;\n", i, i);
        }
    }
    sb_append(&sb, "};\n\n");

    /* Output structure. The names are the ones the pixel shaders ask for
     * (d3d8_shaders.c and d3d8_combiners.c, PS_IN): fog at TEXCOORD4 and a
     * view-space position for table fog at TEXCOORD5. An earlier version
     * wrote fog to FOG, which no pixel shader here reads, so fog never
     * reached them.
     *
     * The names are not the whole contract: the stages are linked by
     * register and component as each compiler packed them, so the types and
     * the order have to match the pixel shaders' too. Every stage declares
     * four float4 texture coordinates, then fog, then the view position.
     * The pixel shaders used to declare float3 coordinates, which packed
     * their `fog` into TEXCOORD3's spare w, so under a program every one of
     * them read oT3.w as its fog factor. */
    sb_append(&sb,
        "struct VS_OUT {\n"
        "    float4 oPos : SV_POSITION;\n"
        "    float4 oD0  : COLOR0;\n"
        "    float4 oD1  : COLOR1;\n"
        "    float4 oT0  : TEXCOORD0;\n"
        "    float4 oT1  : TEXCOORD1;\n"
        "    float4 oT2  : TEXCOORD2;\n"
        "    float4 oT3  : TEXCOORD3;\n"
        "    float  oFog : TEXCOORD4;\n"
        "    float4 oViewPos : TEXCOORD5;\n"
        "    float  oPts : PSIZE;\n"
        "    float4 oB0  : TEXCOORD6;\n"
        "    float4 oB1  : TEXCOORD7;\n"
        "};\n\n");

    /* Main function */
    sb_append(&sb, "VS_OUT main(VS_IN input) {\n");

    /* Declare temporary registers R0-R12 */
    sb_append(&sb, "    /* Temporary registers */\n");
    for (i = 0; i <= 12; i++) {
        sb_append(&sb, "    float4 R%d = float4(0,0,0,0);\n", i);
    }

    /* Address register */
    sb_append(&sb, "    int a0 = 0;\n\n");

    /* Input registers: from the vertex when the layout feeds them, otherwise
     * the register's current value (VSH_VertexData). */
    sb_append(&sb, "    /* Input registers */\n");
    for (i = 0; i < NV2A_VS_MAX_INPUTS; i++) {
        if (inputs & (1u << i)) {
            sb_append(&sb, "    float4 v%d = xboxVertexFed[%d][%d] != 0 ? input.v%d : "
                      "xboxVertexData[%d];\n", i, i / 4, i % 4, i, i);
        }
    }
    sb_append(&sb, "\n");

    /* Output register variables: w of 1, and fog of 1 -- no fog -- for a
     * program that never writes it (Cxbx-Reloaded, CxbxVertexShaderTemplate.hlsl;
     * test case there: Lego Star Wars II) */
    sb_append(&sb,
        "    /* Output registers */\n"
        "    float4 oPos = float4(0,0,0,1);\n"
        "    float4 oD0  = float4(0,0,0,1);\n"
        "    float4 oD1  = float4(0,0,0,1);\n"
        "    float4 oFog = float4(1,1,1,1);\n"
        "    float4 oPts = float4(0,0,0,0);\n"
        "    float4 oB0  = float4(0,0,0,1);\n"
        "    float4 oB1  = float4(0,0,0,1);\n"
        "    float4 oT0  = float4(0,0,0,1);\n"
        "    float4 oT1  = float4(0,0,0,1);\n"
        "    float4 oT2  = float4(0,0,0,1);\n"
        "    float4 oT3  = float4(0,0,0,1);\n"
        "\n");

    /* R12 is aliased to oPos on NV2A */
    sb_append(&sb, "    /* R12 is aliased to oPos */\n");
    sb_append(&sb, "    #define R12 oPos\n\n");

    /* Emit instructions */
    sb_append(&sb, "    /* --- Program body (%d instructions) --- */\n",
              program->length);

    for (i = 0; i < program->length; i++) {
        const NV2AVshInstruction *inst = &program->insns[i];

        int mac_runs = inst->mac_op != NV2A_VSH_MAC_NOP
                    && (unsigned)inst->mac_op < NV2A_VSH_MAC_COUNT;
        int ilu_runs = inst->ilu_op != NV2A_VSH_ILU_NOP;

        sb_append(&sb, "\n    /* Instruction %d */\n    {\n", i);

        /* The two units run in parallel: both read the register file as it
         * was at the start of the slot. So every value is computed into a
         * local first -- _mac, _a0, _ilu -- and only then written. Emitting
         * the MAC write before evaluating the ILU let an ILU source read a
         * temp the MAC had just changed, which the CPU interpreter never did.
         * MAC opcodes 14 and 15 do not exist and emit nothing. */
        if (mac_runs)
            emit_mac_op(&sb, inst);
        if (ilu_runs)
            emit_ilu_op(&sb, inst);

        /* oPos written straight from an input: note where position came
         * from. See g_emit_pos_input. */
        if (mac_runs && inst->mac_op == NV2A_VSH_MAC_MOV &&
            inst->mac_dst.output_reg == NV2A_VSH_OUT_POS &&
            inst->mac_src[0].reg_type == NV2A_VSH_REG_INPUT)
            g_emit_pos_input = inst->mac_src[0].reg_index;

        if (mac_runs && inst->mac_op == NV2A_VSH_MAC_ARL)
            sb_append(&sb, "        a0 = _a0;\n");
        else if (mac_runs)
            emit_dest_assign(&sb, &inst->mac_dst, "_mac");
        if (ilu_runs)
            emit_dest_assign(&sb, &inst->ilu_dst, "_ilu");

        sb_append(&sb, "    }\n");
    }

    /* Undo the R12 alias */
    sb_append(&sb, "\n    #undef R12\n\n");

    /* Xbox programs end in the XDK's screen-space transform, oPos in
     * render-target pixels with Z scaled by the depth buffer. Undo it to clip
     * space (Cxbx-Reloaded, CxbxVertexShaderTemplate.hlsl), when the caller
     * has said what the screen is. */
    sb_append(&sb,
        "    /* Undo the Xbox screen-space transform */\n"
        "    if (xboxScreenspaceEnable.x != 0) {\n"
        "        oPos -= xboxScreenspaceOffset;\n"
        "        oPos /= xboxScreenspaceScale;\n"
        "        if (oPos.w == 0) oPos.w = 1;\n"
        "        oPos.xyz *= oPos.w;\n"
        "    }\n\n");

    /* Populate output structure */
    sb_append(&sb,
        "    /* Write outputs */\n"
        "    VS_OUT o;\n"
        "    o.oPos = oPos;\n"
        "    o.oD0  = saturate(oD0);\n"  /* Colors clamped to [0,1] */
        "    o.oD1  = saturate(oD1);\n"
        "    o.oT0  = oT0;\n"
        "    o.oT1  = oT1;\n"
        "    o.oT2  = oT2;\n"
        "    o.oT3  = oT3;\n"
        "    o.oFog = oFog.x;\n"
        /* Table fog reads -viewpos.z (or its length, for range fog) as the
         * distance; on the Xbox that distance is the program's oFog. */
        "    o.oViewPos = float4(0, 0, -oFog.x, 1);\n"
        "    o.oPts = oPts.x;\n"
        "    o.oB0  = saturate(oB0);\n"
        "    o.oB1  = saturate(oB1);\n"
        "    return o;\n"
        "}\n");

    return sb.pos;
}
