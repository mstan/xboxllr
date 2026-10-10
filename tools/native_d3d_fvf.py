"""Adapt the pinned MIT fixed-function VS to Xbox pretransformed vertices."""


def prepare_fvf(code: str, *, phobos: bool = False) -> str:
    def replace(old, new):
        nonlocal code
        if code.count(old) != 1:
            raise ValueError('unreviewed donor FVF integration: '+old[:70])
        code=code.replace(old,new)

    # The original Xbox table and public FVF encoding both use 2,3,4,1.
    if phobos:
        if 'static const UINT floats[4] = { 2, 3, 4, 1 };' not in code:
            raise ValueError('unreviewed Phobos FVF coordinate dimensions')
    else:
        replace('return field == 0 ? 2 : field;', 'return ((field + 1) & 3) + 1;')
    replace('    "    uint4    TexCoordIndex;\\n"',
            '    "    float4 NativePositionScale;\\n"\n'
            '    "    float4 NativePositionOffset;\\n"\n'
            '    "    uint4    TexCoordIndex;\\n"')
    replace('    UINT  texcoord_index[4];',
            '    float native_position_scale[4],native_position_offset[4];\n    UINT  texcoord_index[4];')
    replace('static void ff_vs_prepare_draw(DWORD fvf)',
            'static UINT pilot_native_fvf;\nstatic float pilot_fvf_scale[4],pilot_fvf_offset[4];\n'
            'static void ff_vs_prepare_draw(DWORD fvf)')
    replace('        if (fvf & D3DFVF_DIFFUSE) cb->flags |= 0x02;',
            '        if(pilot_native_fvf) {\n'
            '            cb->flags |= 0x20 | (tex_count << 8);\n'
            '            memcpy(cb->native_position_scale,pilot_fvf_scale,16);\n'
            '            memcpy(cb->native_position_offset,pilot_fvf_offset,16);\n'
            '        }\n        if (fvf & D3DFVF_DIFFUSE) cb->flags |= 0x02;')
    # Original FVF microcode uses RCP, including infinite W for title RHW=0.
    # Bound the native clip representation; equal-zero 2D vertices keep their
    # screen positions/interpolation. Extreme/mixed RHW hardware equivalence
    # remains outside the qualified scope.
    replace('    "VS_OUT main(VS_IN input) {\\n"',
            '    "float native_rcc(float x) { float r=1/x; return (asuint(r)&0x80000000u) ? -clamp(abs(r),5.42101e-20,1.8446744e19) : clamp(r,5.42101e-20,1.8446744e19); }\\n"\n'
            '    "float4 native_tex(uint stage, VS_IN input) {\\n"\n'
            '    "    uint src = TexCoordIndex[stage];\\n"\n'
            '    "    if (src >= ((Flags >> 8) & 15)) return float4(0,0,0,1);\\n"\n'
            '    "    if (src == 1) return input.tex1;\\n"\n'
            '    "    if (src == 2) return input.tex2;\\n"\n'
            '    "    if (src == 3) return input.tex3;\\n"\n'
            '    "    return input.tex0;\\n"\n'
            '    "}\\n"\n    "VS_OUT main(VS_IN input) {\\n"')
    replace('    "        o.pos.w = 1.0;\\n"',
            '    "        o.pos.w = 1.0;\\n"\n'
            '    "        if (Flags & 0x20u) {\\n"\n'
            '    "            precise float2 window = input.pos.xy + float2(NativePositionScale.w,NativePositionOffset.w);\\n"\n'
            '    "            float2 snapped = trunc(window * 16.0) * 0.0625;\\n"\n'
            '    "            o.pos.xy = snapped * NativePositionScale.xy + NativePositionOffset.xy;\\n"\n'
            '    "            o.pos.z = input.pos.z * NativePositionScale.z + NativePositionOffset.z;\\n"\n'
            '    "        }\\n"')
    replace('    "        if (input.pos.w != 0.0) o.pos /= input.pos.w;\\n"',
            '    "        if (Flags & 0x20u) o.pos *= native_rcc(input.pos.w);\\n"\n'
            '    "        else if (input.pos.w != 0.0) o.pos /= input.pos.w;\\n"')
    for i in range(4):
        replace(f'    "        o.tex{i} = input.tex{i};\\n"',
                f'    "        o.tex{i} = (Flags & 0x20u) ? native_tex({i}, input) : input.tex{i};\\n"')
    code+='\nvoid pilot_d3d_fvf_prepare(UINT fvf,const float scale[4],const float offset[4]) {\n'
    code+='    pilot_native_fvf=1;memcpy(pilot_fvf_scale,scale,16);memcpy(pilot_fvf_offset,offset,16);\n'
    code+='    ff_vs_prepare_draw(fvf);pilot_native_fvf=0;\n}\n'
    # Our programmable VS now supplies a fog factor before interpolation.
    # Preserve the donor's own FVF/table-fog path for its other callers.
    code='static unsigned pilot_native_program_fog;\n'+code
    replace('            if (rs[D3DRS_FOGTABLEMODE] != D3DFOG_NONE) {\n'
            '                /* Table fog: compute the factor per-pixel from view-space depth */',
            '            if (pilot_native_program_fog) { pc->ps_flags |= 2; }\n'
            '            else if (rs[D3DRS_FOGTABLEMODE] != D3DFOG_NONE) {\n'
            '                /* Table fog: compute the factor per-pixel from view-space depth */')
    replace('    "        current.rgb = lerp(FogColor.rgb, current.rgb, input.fog);\\n"',
            '    "        current.rgb = lerp(FogColor.rgb, current.rgb, saturate(input.fog));\\n"')
    code+='\nvoid pilot_d3d_program_fog_mode(unsigned program) { pilot_native_program_fog=program; }\n'
    return code
