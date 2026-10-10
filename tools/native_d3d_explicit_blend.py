"""GPU float blend/final byte packing for a proved nonoverlapping native draw.

The caller supplies a per-draw geometry/state proof; this module additionally
recognizes the parsed four-tap shader semantics. Neither input IDs nor guest
handles, observed images or an enable setting participate in selection.
"""


def prepare_blend_combiners(code: str) -> str:
    def once(old, new):
        nonlocal code
        if code.count(old) != 1:
            raise ValueError('unreviewed explicit blend integration: ' + old[:70])
        code = code.replace(old, new)

    once('static RhiShader *g_last_shader;', '''static RhiShader *g_last_shader;
/* One semantic family, retained independently of the ordinary shader LRU. */
static RhiShader *pilot_blend_shader;
static BOOL pilot_blend_candidate, pilot_blend_selected, pilot_blend_family;
static uint32_t pilot_blend_compiles, pilot_blend_original_compiles;
static BOOL pilot_blend_family_match(const NV2ACombinerState *state);
void pilot_donor_blend_candidate(int admitted) {
    pilot_blend_candidate=admitted!=0;pilot_blend_selected=FALSE;
}
int pilot_donor_blend_active(void) { return pilot_blend_selected!=0; }
void pilot_donor_blend_shader_stats(uint32_t *variants,uint32_t *originals) {
    *variants=pilot_blend_compiles;*originals=pilot_blend_original_compiles;
}''')
    once('static RhiShader *compile_combiner_shader(const NV2ACombinerState *state)',
         'static RhiShader *compile_combiner_shader(const NV2ACombinerState *state, BOOL float_blend)')
    once('    char hlsl[16384];', '    char hlsl[32768];')
    once('    {\n        /* Debug switch, RECOMP_D3D8_PS_DUMP=', '''    if (float_blend) {
        const char *terminal="    return result;\\n}\\n";
        const char *prefix="Texture2D<float4> pilot_blend_destination : register(t4);\\n";
        const char *tail="    precise float4 pilot_source = saturate(result);\\n"
          "    float4 pilot_destination = pilot_blend_destination.Load(int3(uint2(input.pos.xy),0));\\n"
          "    precise float4 pilot_blended = pilot_source*pilot_source.a + pilot_destination*(1.0-pilot_source.a);\\n"
          "    return floor(saturate(pilot_blended)*255.0+0.5)/255.0;\\n}\\n";
        char original[32768];char *at=strstr(hlsl,terminal);size_t head;
        if (!at || at+strlen(terminal)!=hlsl+len || strstr(at+1,terminal))
            pilot_d3d_backend_stop("explicit blend terminal shader anchor changed");
        for (unsigned stage=0;stage<4;++stage) if (state->shadow[stage])
            pilot_d3d_backend_stop("explicit blend cannot use shadow return");
        strcpy(original,hlsl);head=(size_t)(at-hlsl);
        if (strlen(prefix)+head+strlen(tail)>=sizeof(hlsl))
            pilot_d3d_backend_stop("explicit blend HLSL capacity exceeded");
        memcpy(hlsl,prefix,strlen(prefix));memcpy(hlsl+strlen(prefix),original,head);
        strcpy(hlsl+strlen(prefix)+head,tail);len=(int)strlen(hlsl);
        ++pilot_blend_compiles;
    } else ++pilot_blend_original_compiles;

    {\n        /* Debug switch, RECOMP_D3D8_PS_DUMP=''')
    # The normal cache never stores the alternate terminal. The dedicated
    # member below also avoids recompiling it after unrelated LRU pressure.
    code = code.replace('compile_combiner_shader(state);', 'compile_combiner_shader(state, FALSE);')
    once('    memset(g_cache, 0, sizeof(g_cache));\n    memset(&g_combiner_state,',
         '''    pilot_blend_shader=NULL;
    pilot_blend_candidate=pilot_blend_selected=pilot_blend_family=FALSE;
    pilot_blend_compiles=pilot_blend_original_compiles=0;
    memset(g_cache, 0, sizeof(g_cache));
    memset(&g_combiner_state,''')
    once('    /* Release all cached shaders */', '''    if (pilot_blend_shader) rhi_shader_destroy(pilot_blend_shader);
    pilot_blend_shader=NULL;
    pilot_blend_candidate=pilot_blend_selected=pilot_blend_family=FALSE;
    pilot_blend_compiles=pilot_blend_original_compiles=0;
    /* Release all cached shaders */''')
    once('    /* Not using combiner shaders - fall back to fixed-function */',
         '    pilot_blend_selected=FALSE;\n\n    /* Not using combiner shaders - fall back to fixed-function */')
    once('        d3d8_combiners_parse_token(g_ps_token, rs, &g_combiner_state);',
         '''        d3d8_combiners_parse_token(g_ps_token, rs, &g_combiner_state);
        pilot_blend_family=pilot_blend_family_match(&g_combiner_state);''')
    once('''    if (!g_last_shader)
        g_last_shader = d3d8_combiners_get_shader(&g_combiner_state);
    ps = g_last_shader;''', '''    {
        static int diagnostic_override=-1;
        BOOL shadow=FALSE;
        if (diagnostic_override<0) {
            const char *show=getenv("RECOMP_D3D8_PS_SHOW");
            diagnostic_override=show && *show;
        }
        for (i=0;i<NV2A_MAX_TEXTURES;++i) shadow|=g_combiner_state.shadow[i]!=0;
        pilot_blend_selected=pilot_blend_candidate && pilot_blend_family && !shadow && !diagnostic_override;
        if (pilot_blend_selected) {
            if (!pilot_blend_shader) pilot_blend_shader=compile_combiner_shader(&g_combiner_state, TRUE);
            ps=pilot_blend_shader;
        } else {
            if (!g_last_shader) g_last_shader=d3d8_combiners_get_shader(&g_combiner_state);
            ps=g_last_shader;
        }
    }''')
    code += '''
/* Compare parsed shader meaning, never a captured payload/hash/selector. All
 * padding is zeroed by parse_token. Constants are outside the structural key.
 * Parsing this canonical description once also preserves the donor's mapping
 * rules for default final pass-through and PROJECT2D texture modes. */
static BOOL pilot_blend_family_match(const NV2ACombinerState *state) {
    static NV2ACombinerState canonical;
    static BOOL initialized;
    if (!initialized) {
        DWORD rs[256]={0};
        static const DWORD alpha[4]={0xd4d81010u,0xd4d930dcu,0xd4da30dcu,0xd4db30dcu};
        static const DWORD rgb[4]={0xc4c80000u,0xc4c920ccu,0xc4ca20ccu,0xc4cb20ccu};
        for (unsigned i=0;i<4;++i) {
            rs[D3DRS_PSALPHAINPUTS0+i]=alpha[i];rs[D3DRS_PSRGBINPUTS0+i]=rgb[i];
            rs[D3DRS_PSALPHAOUTPUTS0+i]=rs[D3DRS_PSRGBOUTPUTS0+i]=i?0xc00u:0xc0u;
        }
        rs[D3DRS_PSCOMBINERCOUNT]=0x11104u;
        rs[D3DRS_PSTEXTUREMODES]=D3D8_PSTEXTUREMODES_XBOX|0x8421u;
        d3d8_combiners_parse_token(0x84u,rs,&canonical);
        initialized=TRUE;
    }
    return combiner_state_equal(state,&canonical);
}
'''
    return code
