"""Extend the pinned MIT donor's fixed-function sampler with real bump work.

Independent implementation of the public Direct3D environment-map contract:
https://learn.microsoft.com/en-us/windows/win32/direct3d9/bump-mapping-formulas
No emulator implementation is copied. Xbox state numbering is translated by
graphics_states.c; these constants use the donor's desktop-style numbering.
"""
def prepare_bump(code: str) -> str:
    def replace(old, new):
        nonlocal code
        if code.count(old) != 1:
            raise ValueError('unreviewed donor bump integration: ' + old[:70])
        code = code.replace(old, new)

    replace('    UINT  alpha_only[4];',
            '    UINT  alpha_only[4];\n    float bump_matrix[4][4];\n    float bump_luminance[4][4];')
    replace('    "    uint4  AlphaOnly;\\n"',
            '    "    uint4  AlphaOnly;\\n"\n'
            '    "    float4 BumpMatrix[4];\\n"\n'
            '    "    float4 BumpLuminance[4];\\n"')
    replace('            pc->stage_color[stage][0] = colorop;',
            '            memcpy(pc->bump_matrix[stage],&tss[D3DTSS_BUMPENVMAT00],16);\n'
            '            memcpy(pc->bump_luminance[stage],&tss[22],8);\n'
            '            pc->stage_color[stage][0] = colorop;')
    # Dependent samples must happen after the preceding bump sample, before
    # the existing color/alpha combiner loop. Preserve all other donor work.
    replace('                        "    texels[%d] = tex%d.Sample(samp%d, input.tex%d.%s);\\n",\n'
            '                        i, i, i, i, dim ? "xyz" : "xy");',
            '                        "    float3 coord%d = input.tex%d;\\n", i, i);\n'
            '        if (i) {\n'
            '            off += snprintf(buf + off, bufsize - off,\n'
            '                "    if (StageColor[%d].x == 22u || StageColor[%d].x == 23u) {\\n"\n'
            '                "        coord%d.xy += float2(dot(BumpMatrix[%d].xz,texels[%d].rg),dot(BumpMatrix[%d].yw,texels[%d].rg));\\n"\n'
            '                "    }\\n", i-1,i-1,i,i-1,i-1,i-1,i-1);\n'
            '        }\n'
            '        off += snprintf(buf + off, bufsize - off,\n'
            '                        "    texels[%d] = tex%d.Sample(samp%d, coord%d.%s);\\n",\n'
            '                        i, i, i, i, dim ? "xyz" : "xy");\n'
            '        if (i) {\n'
            '            off += snprintf(buf + off, bufsize - off,\n'
            '                "    if (StageColor[%d].x == 23u) texels[%d].rgb *= saturate(texels[%d].b * BumpLuminance[%d].x + BumpLuminance[%d].y);\\n",\n'
            '                i-1,i,i-1,i-1,i-1);\n'
            '        }')
    # Bump stages perturb the next sample; they do not replace the incoming
    # color/alpha with the bump map's signed channels.
    replace('    "        uint colorarg1 = StageColor[i].y;\\n"',
            '    "        if (colorop == 22u || colorop == 23u) continue;\\n"\n'
            '    "        uint colorarg1 = StageColor[i].y;\\n"')
    return code
