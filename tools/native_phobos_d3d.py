#!/usr/bin/env python3
"""Prepare Phobos's whole RHI/D3D11 renderer for the private native title pilot.

Keep guest ABI/resource ownership in our adapter. Retain the donor renderer,
resource conversions and caches; adapt its shader interface to our NV2A VS.
All donor implementation copies stay under ignored build output.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
from native_d3d_donor import bodies, COM
from native_d3d_bump import prepare_bump
from native_d3d_raster import prepare_raster
from native_d3d_fvf import prepare_fvf
from native_d3d_feedback import prepare_feedback
from native_d3d_explicit_blend import prepare_blend_combiners
from native_d3d_texture import prepare_texture_views, prepare_projected_mips, prepare_xrgb_border

PIN = 'bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0'
GIT = 'C:/Program Files/Git/cmd/git.exe'
PHOBOS_COM = COM | {'sf_AddRef', 'sf_Release'}


def once(code, old, new):
    if code.count(old) != 1:
        raise ValueError('unreviewed Phobos integration: '+old[:100])
    return code.replace(old, new)


def prepare_sampling_format(code, combiners=False):
    """Classify sampling from the live guest header, retaining allocation format.

    A depth texture reinterpreted as ARGB must select ordinary texture sampling,
    not the donor's automatic shadow compare. Its shader-key transitions and
    cached constant preparation remain handled by the renderer.
    """
    code = once(code, '#include "d3d8_internal.h"',
                '#include "d3d8_internal.h"\n'
                'DWORD pilot_d3d_texture_sampling_format(UINT stage,DWORD allocation);')
    stage = 'i' if combiners else 'stage'
    code = once(code, 'D3DFORMAT format = d3d8_base_format(tex);',
                'D3DFORMAT format = pilot_d3d_texture_sampling_format('+stage+',d3d8_base_format(tex));')
    if combiners:
        code = once(code, 'D3DFORMAT f = d3d8_base_format(d3d8_GetStageTexture(i));',
                    'D3DFORMAT f = pilot_d3d_texture_sampling_format(i,d3d8_base_format(d3d8_GetStageTexture(i)));')
    return code


def shaders(code):
    # Reuse the existing dependent-bump and homogeneous-coordinate adapter.
    # Phobos already has float4 texture varyings and linear texture metadata.
    code = once(code,
        '                        dim ? "    texels[%d] = tex%d.Sample(samp%d, input.tex%d.xyz);\\n"\n'
        '                            : "    texels[%d] = tex%d.Sample(samp%d, input.tex%d.xy * TexScale[%d].xy);\\n",\n'
        '                        i, i, i, i, i);',
        '                        "    texels[%d] = tex%d.Sample(samp%d, input.tex%d.%s);\\n",\n'
        '                        i, i, i, i, dim ? "xyz" : "xy");')
    code = prepare_bump(code)
    code = prepare_raster(code, phobos=True)
    code = prepare_fvf(code, phobos=True)
    # Guest draws (including FVF) use our native raster adapter, which supplies
    # the texture scale. The host DrawPrimitiveUP helper uses normalized UVs.
    start = code.index('    g_draw_is_program = 0;', code.index('void d3d8_shaders_prepare_draw('))
    end = code.index('    /* Pixel state is independent', start)
    vertex = code[start:end]
    code = code[:start] + '    g_draw_is_program = !!pilot_native_raster;\n\n' + code[end:]
    code = once(code, 'void d3d8_shaders_prepare_draw(DWORD handle)',
                      'void d3d8_shaders_prepare_pixel(void)')
    code += '\nvoid d3d8_shaders_prepare_draw(DWORD handle) {\n'+vertex+'    d3d8_shaders_prepare_pixel();\n}\n'
    code += '\nvoid pilot_gfx_matrix_multiply(float *out,const float *a,const float *b) { mat4_mul(out,a,b); }\n'
    for old, reason in (
        ('if (!rhi_device_ready()) return;', 'missing native device'),
        ('if (!g_ps_cb) return;', 'missing pixel constants'),
        ('if (!ps) return;', 'pixel shader compilation failed')):
        code = code.replace(old, 'if ('+old.split('if (',1)[1].split(') return;',1)[0]+') pilot_d3d_backend_stop("'+reason+'");')
    # Failed upload/layout cannot silently leave stale pipeline state.
    code = code.replace('    if (mapped) {', '    if (!mapped) pilot_d3d_backend_stop("shader constant upload failed");\n    if (mapped) {')
    return code


def prepare(donor, output, reference):
    if subprocess.check_output([GIT,'-C',str(donor),'rev-parse','HEAD'],text=True).strip() != PIN:
        raise ValueError('unreviewed Phobos revision')
    subprocess.run([GIT,'-C',str(donor),'diff','--exit-code','HEAD','--','src/d3d','src/config','src/kernel/nv2a_vsh.c','src/kernel/nv2a_vsh.h','src/platform/xbox_winnt.h','LICENSE','LICENSE.upstream-MIT','NOTICE','LICENSES'],check=True)
    if 'build' not in output.resolve().parts:
        raise ValueError('donor copies must stay in private build output')
    output.mkdir(parents=True,exist_ok=True)
    hashes={}
    paths=list((donor/'src/d3d').glob('*.[ch]'))+list((donor/'src/config').glob('*.[ch]'))
    paths += [donor/name for name in ('src/kernel/nv2a_vsh.c','src/kernel/nv2a_vsh.h','src/platform/xbox_winnt.h','LICENSE','LICENSE.upstream-MIT','NOTICE')]
    paths += [p for p in (donor/'LICENSES').rglob('*') if p.is_file()]
    for path in paths:
        relative=path.relative_to(donor)
        code=path.read_text(encoding='utf-8')
        if relative.parts[:2]==('src','d3d') and path.suffix=='.c':
            # Do not inherit the donor's success-only COM bodies. The guest
            # adapter uses the bounded real methods, internal resources/RHI.
            for name,start,end in reversed(list(bodies(code))):
                if name not in PHOBOS_COM:
                    code=code[:start]+'{ pilot_d3d_backend_stop("unqualified backend method: '+name+'"); }'+code[end:]
            if path.name=='d3d8_shaders.c': code=prepare_sampling_format(shaders(code))
            if path.name=='d3d8_resources.c': code=prepare_xrgb_border(code)
            if path.name=='d3d8_combiners.c':
                code=prepare_sampling_format(code, combiners=True)
                code=prepare_projected_mips(code)
                # A zero-stage final combiner is real work, not FFP. The
                # adapter's nonzero selector distinguishes it from PS disable.
                code=code.replace('    if (state->num_stages < 1) state->num_stages = 1;','')
                start=code.index('    if (!rhi_device_ready() || !g_combiner_cb)',code.index('BOOL d3d8_combiners_prepare_draw(void)'))
                code=code[:start]+code[start:].replace('return FALSE;','pilot_d3d_backend_stop("combiner shader could not execute");')
                code=code.replace('    if (mapped) {','    if (!mapped) pilot_d3d_backend_stop("combiner constants upload failed");\n    if (mapped) {')
                code=prepare_blend_combiners(code)
            if path.name=='d3d8_states.c':
                code=prepare_texture_views(code)
                code,count=re.subn(r'    if \(min_linear && mag_linear && mip_linear\).*?    return RHI_FILTER_POINT;',
                    '    return (min_linear ? 0x10u : 0u) | (mag_linear ? 4u : 0u) | (mip_linear ? 1u : 0u);',code,flags=re.S)
                if count!=1:raise ValueError('unreviewed Phobos filter mapping')
                code=once(code,'    sd.address_w = RHI_ADDRESS_WRAP;','    sd.address_w = d3d8_to_rhi_address(tss[31]);')
                code=once(code,'    sd.max_lod = 3.402823466e+38f;',
                    '    memcpy(&sd.mip_lod_bias,&tss[D3DTSS_MIPMAPLODBIAS],4);\n'
                    '    sd.min_lod=(FLOAT)tss[D3DTSS_MAXMIPLEVEL];\n'
                    '    sd.max_lod=3.402823466e+38f;\n'
                    '    DWORD border=tss[D3DTSS_BORDERCOLOR];\n'
                    '    sd.border[0]=((border>>16)&255)/255.0f;\n'
                    '    sd.border[1]=((border>>8)&255)/255.0f;\n'
                    '    sd.border[2]=(border&255)/255.0f;\n'
                    '    sd.border[3]=((border>>24)&255)/255.0f;')
            if path.name=='d3d8_device.c':
                code=prepare_feedback(code)
                code=once(code,'        d3d8_display_output_shape(s->width, s->height, &shape_w, &shape_h);',
                    '        d3d8_display_output_shape(s->width, s->height, &shape_w, &shape_h);\n'
                    '        pilot_d3d_present_shape(&shape_w,&shape_h);')
                code='void pilot_d3d_present_shape(unsigned *width,unsigned *height);\n'+code
                # The donor's function-local empty-stage view survives device
                # destruction. Rebinding it to a new D3D11 device corrupts the
                # context and can leave real occlusion queries pending forever.
                code=once(code,'static void up_ring_shutdown(void);',
                    'static void up_ring_shutdown(void);\nstatic void empty_stage_shutdown(void);')
                code=once(code,'        up_ring_shutdown();',
                    '        empty_stage_shutdown();\n        up_ring_shutdown();')
                start=code.index('static RhiView *empty_stage_view(void)')
                end=code.index('\nstatic HRESULT __stdcall dev_SetTexture',start)
                body=code[start:end].replace('    static RhiView *view;\n    static int tried;\n','')
                body=re.sub(r'\bview\b','empty_stage_cached_view',body)
                body=re.sub(r'\btried\b','empty_stage_tried',body)
                body=('static RhiView *empty_stage_cached_view;\nstatic int empty_stage_tried;\n'+body+
                    '\nstatic void empty_stage_shutdown(void) {\n'
                    '    if(empty_stage_cached_view)rhi_view_destroy(empty_stage_cached_view);\n'
                    '    empty_stage_cached_view=NULL;empty_stage_tried=0;\n}\n')
                code=code[:start]+body+code[end:]
                code=once(code,'    dd.windowed = pp->Windowed ? 1 : 0;',
                    '    dd.windowed = pp->Windowed ? 1 : 0;\n'
                    '    dd.debug = getenv("XLLE_PILOT_D3D_DEBUG") != NULL;')
                code+='\nHRESULT pilot_donor_upload_vertices(const void *data,UINT bytes,UINT stride) {\n'
                code+='    UINT offset=up_ring_upload(&g_up_vertex_ring,data,bytes);\n'
                code+='    if(offset==(UINT)-1)return E_OUTOFMEMORY;\n'
                code+='    rhi_set_vertex_buffer(0,g_up_vertex_ring.buffer,stride,offset);return S_OK;\n}\n'
                code+='\n'+(Path(__file__).resolve().parents[1]/'pilots/native-title/graphics_phobos_reset.inc').read_text()
                code=once(code,'return (HRESULT)rhi_present(g_present_interval);','return (HRESULT)rhi_present(0);')
                code=re.sub(r'/\* Non-fatal:[^*]+\*/','dev_Release(&g_device); return hr;',code)
                # SetTexture binds a resource; it must not rewrite COLOROP.
                code=once(code,'        /* Mark texture stage as active */\n'
                    '        if (g_device_state.tss[Stage][D3DTSS_COLOROP] == D3DTOP_DISABLE)\n'
                    '            g_device_state.tss[Stage][D3DTSS_COLOROP] = D3DTOP_MODULATE;','')
                code=code.replace('        g_device_state.tss[Stage][D3DTSS_COLOROP] = D3DTOP_DISABLE;','')
            if path.name=='rhi_d3d11.c':
                code+='\nID3D11Device *d3d8_GetD3D11Device(void) { return g_dev; }\n'
                code+='ID3D11DeviceContext *d3d8_GetD3D11Context(void) { return g_ctx; }\n'
            code=('/* Private pilot adaptation of https://github.com/phobos665/xboxrecomp\n'
                  ' * revision '+PIN+', '+relative.as_posix()+'.\n'
                  ' * Original notices below are retained. See ../../MODIFICATIONS.md\n'
                  ' * and the prepared root LICENSE, LICENSE.upstream-MIT and NOTICE. */\n'
                  '#include "pilot_d3d_checks.h"\n'+code)
        destination=output/relative
        destination.parent.mkdir(parents=True,exist_ok=True)
        if relative.parts[0].startswith('LICENSE') or relative.name=='NOTICE':
            destination.write_bytes(path.read_bytes()) # preserve full upstream notice bytes
        else:destination.write_text(code,encoding='utf-8',newline='\n')
        hashes[str(relative).replace('\\','/')]=dict(source=hashlib.sha256(path.read_bytes()).hexdigest(),prepared=hashlib.sha256(destination.read_bytes()).hexdigest())
    (output/'pilot_d3d_checks.h').write_text('#pragma once\n#include <windows.h>\n__declspec(noreturn) void pilot_d3d_backend_stop(const char *reason);\nHRESULT pilot_d3d_require(HRESULT result,const char *file,unsigned line);\n')
    # Reuse the independently pinned decoder already prepared for the pilot.
    manifest=json.loads((reference/'donor.json').read_text())
    if 'nv2a_reference' not in manifest: raise ValueError('missing pinned NV2A decoder')
    dest=output/'nv2a-reference';dest.mkdir(exist_ok=True)
    for path in (reference/'nv2a-reference').iterdir():
        if path.is_file(): (dest/path.name).write_bytes(path.read_bytes())
    (output/'MODIFICATIONS.md').write_text(
        '# Local adaptations to Phobos xboxrecomp\n\n'
        'Source: https://github.com/phobos665/xboxrecomp/tree/'+PIN+'\n\n'
        'Based on sp00nznet/xboxrecomp; upstream MIT notice retained separately.\n'
        'The Phobos fork declares GPL-3.0; this is a private reference build,\n'
        'outside the intended non-GPL product. Original per-file notices remain.\n\n'
        'Our changes: terminal guards replace unqualified COM bodies; host\n'
        'presentation defers pacing to the Xbox API adapter; renderer accessors\n'
        'and UP-ring upload expose real owned resources; independent vertex\n'
        'translation shares the pixel compiler; existing FVF/raster/bump and\n'
        'sampler corrections are retained; sampling classification uses the\n'
        'live caller header, including depth storage reinterpreted as ARGB.\n'
        'Pretransformed screen+bias is rounded and truncated to four subpixel\n'
        'bits on the GPU before viewport mapping, matching the qualified\n'
        'original software FVF contract. Exact silicon precision remains separate.\n'
        'Programmable raster output applies the same four-bit window XY\n'
        'truncation before viewport mapping; raw shader outputs stay intact.\n'
        'Combiner failures cannot fall back\n'
        'to an unrelated shader; the empty-stage RHI view is released/reset on\n'
        'device teardown. XLLE_PILOT_D3D_DEBUG enables native layer diagnostics.\n'
        'Independent Xbox reset adapter reuses real presentation-buffer creation\n'
        'while retaining the native device, resources, shaders and state caches.\n'
        'Programmable fog is evaluated per vertex in the native translator;\n'
        'the pixel adapter consumes its interpolated, late-clamped factor.\n'
        'The existing feedback scratch/copy is exposed to native draws and target\n'
        'binding. Failed copies stop; scratch resources are released on teardown.\n'
        'A parsed four-tap combiner and current vertex/state proof select GPU\n'
        'float destination blending before final byte packing by default. Its\n'
        'dedicated snapshot and shader survive ordinary feedback/cache churn\n'
        'and are released with their owning device. Source color is not packed\n'
        'before blending; unsupported geometry retains the ordinary draw route.\n'
        'Original and prepared hashes are in donor.json.\n',encoding='utf-8')
    result=dict(repository='https://github.com/phobos665/xboxrecomp',donor=PIN,backend='PHOBOS',
                license='GPL-3.0 fork; retained MIT upstream and per-file notices',
                license_files=[str(p.relative_to(donor)).replace('\\','/') for p in paths if p.relative_to(donor).parts[0].startswith('LICENSE') or p.name=='NOTICE'],
                scope='private reference pilot',files=hashes,nv2a_reference=manifest['nv2a_reference'])
    (output/'donor.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('donor','output','reference'): parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    result=prepare(args.donor,args.output,args.reference)
    print(json.dumps(dict(donor=result['donor'],files=len(result['files']))))
