"""Supply NV2A BUMPENVMAP work missing from the pinned private renderer.

The dependent lookup is nonprojective. Unsigned B/G channels are interpreted
as signed bytes before the destination stage's matrix transforms them. Public
NV_texture_shader and xemu psh.c document this contract. Signed texture storage
and BUMPENVMAP_LUM remain outside the admitted guest format/program set.
"""


def prepare_combiner_bump(code, *, header=False):
    def once(old, new):
        nonlocal code
        if code.count(old) != 1:
            raise ValueError('unreviewed combiner bump boundary: ' + old[:80])
        code = code.replace(old, new)

    if header:
        once('    UINT  shadow_pad[2];',
             '    UINT  shadow_pad[2];\n'
             '    float bump_matrix[NV2A_MAX_TEXTURES][4];')
        return code

    once(' * psh.c has it, with these differences: BUMPENVMAP and BUMPENVMAP_LUM look\n'
         ' * up their own coordinates without the bump offset (the bump-environment\n'
         ' * matrix is a texture stage state not forwarded yet), CLIPPLANE kills',
         ' * psh.c has it, with these differences: BUMPENVMAP_LUM is not admitted;\n'
         ' * unsigned BUMPENVMAP uses live matrix constants below. CLIPPLANE kills')
    once('        case XTM_PROJECT2D:\n        case XTM_BUMPENVMAP:\n',
         '        case XTM_PROJECT2D:\n')
    once('        case XTM_PROJECT3D:\n            EMIT(',
         '''        case XTM_BUMPENVMAP:
            if (i < 1)
                break;
            /* NV2A unsigned source bytes become signed B/G offsets. Matrix
             * columns are 00,01,10,11 in host TSS storage; Xbox's adapter
             * has already exchanged its final two API slots. No q divide. */
            EMIT("    { float2 d=dotmap3(r_t%d).bg;\\n",in);
            EMIT("      float2 uv=(input.tc%d.xy+float2(dot(bump_matrix[%d].xz,d),dot(bump_matrix[%d].yw,d)))*tex_scale[%d].xy;\\n",i,i,i,i);
            EMIT("      r_t%d=tex%d.Sample(samp%d,uv); }\\n",i,i,i);
            break;
        case XTM_PROJECT3D:
            EMIT(''')
    # The C tail follows shadow_func/padding, so place the HLSL tail there too.
    once('    EMIT("    uint4  shadow_func;\\n");    /* x: D3DCMPFUNC, y: compare reversed */',
         '    EMIT("    uint4  shadow_func;\\n");    /* x: D3DCMPFUNC, y: compare reversed */\n'
         '    EMIT("    float4 bump_matrix[4];\\n");')
    once('            cb->alpha_only[i] = format == D3DFMT_A8 || format == D3DFMT_LIN_A8;',
         '''            cb->alpha_only[i] = format == D3DFMT_A8 || format == D3DFMT_LIN_A8;
            const DWORD *bump_tss=d3d8_GetTSS(i);
            if(bump_tss)memcpy(cb->bump_matrix[i],&bump_tss[D3DTSS_BUMPENVMAT00],16);
            else memset(cb->bump_matrix[i],0,16);''')
    return code
