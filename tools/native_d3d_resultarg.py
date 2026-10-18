"""Retain independent CURRENT/TEMP fixed-function pixel results.

Use an existing reserved constant lane; state changes do not compile shaders.
Apply only to the reviewed donor source shapes, before writing prepared copies.
"""

def once(code, old, new):
    if code.count(old)!=1:raise ValueError('unreviewed fixed-function TEMP integration: '+old[:100])
    return code.replace(old,new)


def prepare_resultarg(code):
    code=once(code,'float4 texel, float4 tfactor, float4 specular) {',
                   'float4 texel, float4 tfactor, float4 specular, float4 temporary) {')
    code=once(code,'    "    else val = current;\\n"',
                   '    "    else if (base == 5u) val = temporary;\\n" /* D3DTA_TEMP */\n'
                   '    "    else val = current;\\n"')
    old='TexFactor, input.specular);'
    if code.count(old)!=4:raise ValueError('unreviewed fixed-function argument readers')
    code=code.replace(old,'TexFactor, input.specular, temporary);')
    code=once(code,'    float4 current = input.diffuse;\\n"',
                   '    float4 current = input.diffuse;\\n"\n'
                   '    "    float4 temporary = float4(0,0,0,0);\\n"')
    code=once(code,'    "        current = float4(color, alpha);\\n"',
                   '    "        float4 result = float4(color, alpha);\\n"\n'
                   '    "        if (StageAlpha[i].z == 5u) temporary = result;\\n"\n'
                   '    "        else current = result;\\n"')
    direct='            pc->stage_alpha[stage][1] = tss[D3DTSS_ALPHAARG2];'
    legacy='            pc->stage_alpha[stage][1] = tss[D3DTSS_ALPHAARG2] ? tss[D3DTSS_ALPHAARG2] : D3DTA_CURRENT;'
    old=direct if direct in code else legacy
    code=once(code,old,old+'\n            pc->stage_alpha[stage][2] = tss[D3DTSS_RESULTARG];')
    return code
