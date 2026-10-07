#!/usr/bin/env python3
"""Prepare a pinned MIT D3D11 backend, excluding unqualified COM service bodies.

Private build-tree copies retain upstream LICENSE/NOTICE. No kernel, APU, NV2A
device, GPL implementation or generated title code is imported by this tool.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

PIN = "1409a7d7801d3e931fb1074be6104209ddd9a33e"
FILES = "d3d8_device.c d3d8_gamma.c d3d8_resources.c d3d8_shaders.c d3d8_states.c d3d8_combiners.c d3d8_vsh.c d3d8_xbox.h d3d8_internal.h d3d8_swizzle.h d3d8_fvf.h d3d8_combiners.h d3d8_vsh.h".split()
# All other COM methods terminate explicitly, including upstream success-only
# caps/display/reset/pixel-constant/priority bodies. Qualify more as reached.
COM = set("dev_AddRef dev_Release dev_BeginScene dev_EndScene dev_Clear dev_SetViewport dev_GetViewport dev_SetTransform dev_GetTransform dev_SetRenderState dev_GetRenderState dev_SetTextureStageState dev_GetTextureStageState dev_SetVertexShader dev_GetVertexShader dev_DrawPrimitiveUP dev_Swap d3d8_AddRef d3d8_Release d3d8_CreateDevice".split())


def lexical(code):
    return re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: " " * len(m[0]), code)


def bodies(code):
    clean = lexical(code)
    for match in re.finditer(r'\b(?:static\s+)?[\w* ]+\b__stdcall\s+(\w+)\s*\([^;{}]*\)\s*\{', clean):
        start = match.end()-1
        depth, end = 1, start+1
        while depth:
            depth += (clean[end] == "{") - (clean[end] == "}")
            end += 1
        yield match[1], start, end


def checked_calls(code, names):
    clean = lexical(code)
    edits = []
    for match in re.finditer(r'\b(' + '|'.join(names) + r')\s*\(', clean):
        depth, end = 1, match.end()
        while depth:
            depth += (clean[end] == "(") - (clean[end] == ")")
            end += 1
        edits.append((match.start(), end))
    for start, end in reversed(edits):
        code = code[:start] + 'pilot_d3d_require(' + code[start:end] + ', __FILE__, __LINE__)' + code[end:]
    return code


def prepare(donor: Path, output: Path):
    git = "C:/Program Files/Git/cmd/git.exe"
    if subprocess.check_output([git, "-C", str(donor), "rev-parse", "HEAD"], text=True).strip() != PIN:
        raise ValueError("unreviewed D3D donor revision")
    subprocess.run([git, "-C", str(donor), "diff", "--exit-code", "HEAD", "--", "src/d3d", "src/platform/xbox_winnt.h", "LICENSE", "NOTICE"], check=True)
    if "build" not in output.resolve().parts:
        raise ValueError("donor copies must stay in private build output")
    output.mkdir(parents=True, exist_ok=True)
    sources, changes = {}, {}
    for name in FILES:
        source = donor / "src/d3d" / name
        sources[name] = hashlib.sha256(source.read_bytes()).hexdigest()
        code = source.read_text(encoding="utf-8")
        omitted = []
        if name.endswith(".c"):
            for method, start, end in reversed(list(bodies(code))):
                if method not in COM:
                    code = code[:start] + '{ pilot_d3d_backend_stop("unqualified backend method: ' + method + '"); }' + code[end:]
                    omitted.append(method)
            if name == "d3d8_device.c":
                # Cleanup real partial initialization; do not mask shader init
                # failure with an unrelated fixed-function substitute.
                name_map = {n:(a,b) for n,a,b in bodies(code)}
                start,end = name_map["d3d8_CreateDevice"]
                body = code[start:end]
                body = body.replace('    if (!pPP || !ppDevice)', '    if (g_device_initialized) return E_FAIL;\n    if (!pPP || !ppDevice)')
                body = body.replace('    memset(&g_device_state,', '    *ppDevice = NULL;\n    memset(&g_device_state,')
                body = body.replace('return hr;', '{ dev_Release(&g_device); return hr; }')
                body = re.sub(r'/\* Non-fatal:[^*]+\*/', 'dev_Release(&g_device); return hr;', body)
                code = code[:start] + body + code[end:]
                code = code.replace('        /* Cleanup D3D11 resources */',
                    '        if (g_device_state.d3d11_context) ID3D11DeviceContext_ClearState(g_device_state.d3d11_context);\n'
                    '        g_cur_vb=NULL; g_cur_ib=NULL; memset(g_cur_textures,0,sizeof(g_cur_textures));\n'
                    '        g_cur_rt=NULL; g_cur_ds=NULL;\n        /* Cleanup D3D11 resources */')
                code = code.replace('        if (converted) draw_data = converted;',
                    '        if (!converted) return E_OUTOFMEMORY;\n        draw_data = converted;')
            if name == "d3d8_states.c":
                code,count = re.subn(r'    if \(min_linear && mag_linear && mip_linear\).*?    return D3D11_FILTER_MIN_MAG_MIP_POINT;',
                    '    return (D3D11_FILTER)((min_linear ? 0x10 : 0) | (mag_linear ? 4 : 0) | (mip_linear ? 1 : 0));',
                    code,flags=re.S)
                if count!=1: raise ValueError('unreviewed basic-filter conversion')
                code = code.replace('sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;',
                                    'sd.AddressW = d3d8_to_d3d11_address(tss[31]);')
                code = code.replace('    sd.MaxLOD = D3D11_FLOAT32_MAX;',
                    '    memcpy(&sd.MipLODBias,&tss[D3DTSS_MIPMAPLODBIAS],4);\n'
                    '    sd.MinLOD=(FLOAT)tss[D3DTSS_MAXMIPLEVEL];\n'
                    '    sd.MaxLOD=tss[D3DTSS_MIPFILTER] ? D3D11_FLOAT32_MAX : sd.MinLOD;\n'
                    '    DWORD border=tss[D3DTSS_BORDERCOLOR];\n'
                    '    sd.BorderColor[0]=((border>>16)&255)/255.0f;\n'
                    '    sd.BorderColor[1]=((border>>8)&255)/255.0f;\n'
                    '    sd.BorderColor[2]=(border&255)/255.0f;\n'
                    '    sd.BorderColor[3]=((border>>24)&255)/255.0f;')
            # These paths otherwise continue drawing with stale/uninitialized
            # buffers/states when a host allocation, map or compilation fails.
            checks = ["ID3D11DeviceContext_Map", "ID3D11Device_CreateInputLayout",
                      "ID3D11Device_CreateVertexShader", "ID3D11Device_CreatePixelShader"]
            if name in ("d3d8_states.c", "d3d8_shaders.c", "d3d8_vsh.c", "d3d8_combiners.c"):
                checks += ["D3DCompile", "ID3D11Device_CreateBlendState", "ID3D11Device_CreateDepthStencilState", "ID3D11Device_CreateRasterizerState", "ID3D11Device_CreateSamplerState"]
            code = checked_calls(code, checks)
            if name == "d3d8_shaders.c":
                # DIFFUSE is argument zero, not an absent state/default.
                for field,default in (("COLORARG1","TEXTURE"),("COLORARG2","CURRENT"),("ALPHAARG1","TEXTURE"),("ALPHAARG2","CURRENT")):
                    code = code.replace(f'tss[D3DTSS_{field}] ? tss[D3DTSS_{field}] : D3DTA_{default}',f'tss[D3DTSS_{field}]')
                code = code.replace('    if (!ctx || !g_vs) return;',
                    '    if (!ctx || !g_vs) pilot_d3d_backend_stop("missing vertex pipeline");')
                code = code.replace('    if (!g_ps_cb) return;',
                    '    if (!g_ps_cb) pilot_d3d_backend_stop("missing pixel constants");')
                code = code.replace('    if (!ps) return;',
                    '    if (!ps) pilot_d3d_backend_stop("pixel shader allocation failed");')
                code = code.replace('    if (layout)\n',
                    '    if (!layout) pilot_d3d_backend_stop("input layout allocation failed");\n    if (layout)\n')
            code = '#include "pilot_d3d_checks.h"\n' + code
        (output/name).write_text(code, encoding="utf-8", newline="\n")
        changes[name] = dict(excluded_com_bodies=sorted(omitted), sha256=hashlib.sha256((output/name).read_bytes()).hexdigest())
    platform = output / "platform"; platform.mkdir(exist_ok=True)
    for relative in ("LICENSE", "NOTICE", "src/platform/xbox_winnt.h"):
        source = donor / relative
        target = platform / source.name if relative.startswith("src/") else output / source.name
        target.write_bytes(source.read_bytes())
        sources[relative] = hashlib.sha256(source.read_bytes()).hexdigest()
    (output/"pilot_d3d_checks.h").write_text(
        '#pragma once\n#include <windows.h>\n'
        '__declspec(noreturn) void pilot_d3d_backend_stop(const char *reason);\n'
        'HRESULT pilot_d3d_require(HRESULT result, const char *file, unsigned line);\n', newline="\n")
    manifest = dict(donor=PIN, license="MIT; upstream LICENSE and NOTICE retained", sources=sources, prepared=changes,
                    policy="unqualified COM bodies excluded; failure cannot silently drop graphics work")
    (output/"donor.json").write_text(json.dumps(manifest, indent=2)+"\n", newline="\n")
    return manifest


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--donor", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args=parser.parse_args()
    result=prepare(args.donor,args.output)
    print(json.dumps(dict(donor=result["donor"], files=len(result["sources"]), excluded=sum(len(v["excluded_com_bodies"]) for v in result["prepared"].values()))))
