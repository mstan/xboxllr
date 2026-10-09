"""Attach authored native texture sampling, storage and view adapters."""
import json


def prepare_projected_mips(code):
    """Keep mip footprint evaluation on the GPU, using the maintained contract.

    D3D's implicit coarse differences/orthogonalized ellipse can select another
    mip under projective coordinates. Recover affine homogeneous planes by
    dividing by DirectX SV_POSITION.w, differentiate at the fragment, and
    give the sampler orthogonal gradients with the required isotropic footprint.
    Filtering, bias, clamps and texture ownership stay with the native sampler.
    Single-level views with equal min/mag filters retain ordinary Sample.
    """
    anchor='    EMIT("float4 main(PS_IN input) : SV_TARGET {\\n");'
    hlsl='''float2 pilot_projected_gradient(float4 h,float2 scale,float2 dimensions) {
    float iq=1/(h.w!=0?h.w:1);
    float2 n=h.xy*scale,uv=n*iq;
    float2 gx=(ddx_coarse(n)-uv*ddx_coarse(h.w))*iq*dimensions;
    float2 gy=(ddy_coarse(n)-uv*ddy_coarse(h.w))*iq*dimensions;
    float rho=sqrt(max(dot(gx,gx),dot(gy,gy)));
    return rho/dimensions;
}
'''
    emitted=''.join('    EMIT('+json.dumps(line)+');\n' for line in hlsl.splitlines(keepends=True))
    old=r'''            EMIT("    r_t%d = tex%d.Sample(samp%d, input.tc%d.xy / qdiv(input.tc%d.w)"
                 " * tex_scale[%d].xy);\n", i, i, i, i, i, i);'''
    new=r'''            EMIT("    { uint w,h,levels; tex%d.GetDimensions(0,w,h,levels);\n",i);
            EMIT("      float2 uv=input.tc%d.xy/qdiv(input.tc%d.w)*tex_scale[%d].xy;\n",i,i,i);
            EMIT("      if(levels>1 || tex_scale[%d].w>0) { float2 g=pilot_projected_gradient(input.tc%d/input.pos.w,tex_scale[%d].xy,float2(w,h));\n",i,i,i);
            /* A one-level view cannot blend another mip. Preserve the ideal
             * positive-LOD min filter choice across native LOD quantization. */
            EMIT("        if(levels==1 && tex_scale[%d].w>0) { float b=g.x*w*exp2(tex_scale[%d].z); if(b>1) g*=max(1.0,1.0078125/b); }\n",i,i);
            EMIT("        r_t%d=tex%d.SampleGrad(samp%d,uv,float2(g.x,0),float2(0,g.y));\n",i,i,i);
            EMIT("      } else { r_t%d=tex%d.Sample(samp%d,uv); } }\n",i,i,i);'''
    scale_anchor='            cb->tex_scale[i][2] = cb->tex_scale[i][3] = 0.0f;'
    scale_new='''            cb->tex_scale[i][2] = 0.0f;
            /* Adapter flag: LOD also selects min/mag on a one-level view. */
            const DWORD *project_tss=d3d8_GetTSS(i);
            if(project_tss)memcpy(&cb->tex_scale[i][2],&project_tss[D3DTSS_MIPMAPLODBIAS],sizeof(float));
            cb->tex_scale[i][3]=project_tss && project_tss[D3DTSS_MINFILTER]!=project_tss[D3DTSS_MAGFILTER] ? 1.0f : 0.0f;'''
    if code.count(anchor)!=1 or code.count(old)!=1 or code.count(scale_anchor)!=1:
        raise ValueError('unreviewed Phobos projected texture sampling boundary')
    return code.replace(anchor,emitted+anchor).replace(old,new).replace(scale_anchor,scale_new)


def prepare_texture_views(code):
    old='            d3d8_states_apply_sampler(s);\n    }\n}'
    if code.count(old)!=1:
        raise ValueError('unreviewed sampler/view binding boundary')
    return ('void pilot_d3d_texture_sampling_views(void);\n'+
            code.replace(old,'            d3d8_states_apply_sampler(s);\n    }\n'
                             '    pilot_d3d_texture_sampling_views();\n}'))


def prepare_xrgb_border(code):
    """Normalize X in the existing upload scratch, retaining RGBA border color.

    Normalized XRGB is CPU-uploaded in this adapter. Its raw shadow/query
    format remains XRGB. Native BGRX sampling also forces border alpha to one,
    unlike the independently documented NV2A texture-border behavior.
    """
    old='''    case D3DFMT_X8R8G8B8:
    case D3DFMT_LIN_X8R8G8B8:
        return RHI_FORMAT_B8G8R8X8_UNORM;'''
    new='''    case D3DFMT_X8R8G8B8:
        return RHI_FORMAT_B8G8R8A8_UNORM;
    case D3DFMT_LIN_X8R8G8B8:
        return RHI_FORMAT_B8G8R8X8_UNORM;'''
    allocation='        linear = (BYTE *)malloc((size_t)w * h * bpp);'
    unpack='            xbox_unswizzle_rect(linear, src_data, w, h, bpp);'
    if any(code.count(anchor)!=1 for anchor in (old,allocation,unpack)):
        raise ValueError('unreviewed Phobos XRGB texture preparation boundary')
    code=code.replace(old,new).replace(allocation,allocation+'''
        if (!linear && fmt == D3DFMT_X8R8G8B8)
            pilot_d3d_backend_stop("XRGB texture preparation allocation failed");''')
    return code.replace(unpack,unpack+'''
            if (fmt == D3DFMT_X8R8G8B8)
                for (size_t pixel = 0; pixel < (size_t)w * h; ++pixel)
                    linear[pixel * 4 + 3] = 255;''')
