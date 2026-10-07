"""Adapt the MIT fixed-function pixel interface for native NV2A vertex output."""


def prepare_raster(code: str, *, phobos: bool = False) -> str:
    for stage in range(0 if phobos else 4):
        # Both the donor fixed-function VS and PS must keep the full coordinate.
        old=f'    "    float3 tex{stage}     : TEXCOORD{stage};\\n"'
        if code.count(old)!=2:raise ValueError('unreviewed donor texture interface')
        code=code.replace(old,old.replace('float3','float4'))
        code=code.replace(f'o.tex{stage} = input.tex{stage}.xyz;',f'o.tex{stage} = input.tex{stage};')
        code=code.replace(f'o.tex{stage} = gen_tex({stage}, input, posCam, nrmCam).xyz;',
                          f'o.tex{stage} = gen_tex({stage}, input, posCam, nrmCam);')

    def replace(old,new):
        nonlocal code
        if code.count(old)!=1:raise ValueError('unreviewed donor raster integration: '+old[:60])
        code=code.replace(old,new)

    replace('    UINT  alpha_only[4];','    UINT  alpha_only[4];\n    UINT native_raster[4];\n    float texture_scale[4][4];')
    replace('    "    uint4  AlphaOnly;\\n"',
            '    "    uint4  AlphaOnly;\\n"\n    "    uint4 NativeRaster;\\n"\n    "    float4 TextureScale[4];\\n"')
    buffer_decl = 'static RhiBuffer *g_ps_cb = NULL;' if phobos else 'static ID3D11Buffer        *g_ps_cb = NULL;'
    replace(buffer_decl,
            'static UINT pilot_native_raster;\nstatic float pilot_texture_scale[4][4];\n'
            'void pilot_d3d_raster_texture_mode(UINT native,const float scale[4][4]) {\n'
            '    pilot_native_raster=native;if(scale)memcpy(pilot_texture_scale,scale,sizeof(pilot_texture_scale));\n}\n'
            + buffer_decl)
    replace('        memset(pc, 0, sizeof(*pc));',
            '        memset(pc, 0, sizeof(*pc));\n        pc->native_raster[0]=pilot_native_raster;\n'
            '        memcpy(pc->texture_scale,pilot_texture_scale,sizeof(pc->texture_scale));')
    replace('"    float3 coord%d = input.tex%d;\\n", i, i);',
            '"    float3 coord%d = input.tex%d.xyz;\\n"\n'
            '                        "    if (NativeRaster.x) coord%d /= input.tex%d.w;\\n", i,i,i,i);')
    replace('"    texels[%d] = tex%d.Sample(samp%d, coord%d.%s);\\n",\n'
            '                        i, i, i, i, dim ? "xyz" : "xy");',
            '"    if (NativeRaster.x) coord%d *= TextureScale[%d].xyz;\\n"\n'
            '                        "    texels[%d] = tex%d.Sample(samp%d, coord%d.%s);\\n",\n'
            '                        i,i,i,i,i,i,dim ? "xyz" : "xy");')
    return code
