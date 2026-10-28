"""Native immutable pipeline object ownership in the canonical donor preparer."""
from pathlib import Path


def once(code,old,new):
    if code.count(old)!=1:raise ValueError('unreviewed native pipeline owner: '+old[:100])
    return code.replace(old,new)


def prepare_pipeline(code):
    header=(Path(__file__).resolve().parents[1]/'pilots/native-title/pipeline_state_cache.h').read_text()
    owner=header+'\n#include <stdlib.h>\n'
    owner+='volatile uint64_t pilot_native_pipeline_counters[12];\n'
    owner+='static int pilot_native_pipeline_enabled(void) { static int enabled=-1; if(enabled<0) { const char *value=getenv("XLLE_PILOT_NATIVE_PIPELINE_CACHE");enabled=!(value && !strcmp(value,"0")); }return enabled; }\n'
    for index,kind in enumerate(('blend','depth','raster')):
        title=kind.title();desc=f'Rhi{title}Desc';state=f'Rhi{title}State'
        owner+=f'''static struct PilotPipelineCache pilot_native_{kind}_cache;
typedef char PilotNative{title}KeyFits[(sizeof({desc})<=PILOT_PIPELINE_KEY_BYTES)?1:-1];
static void *pilot_native_make_{kind}(const void *key) {{return rhi_{kind}_state_create((const {desc}*)key);}}
static void pilot_native_free_{kind}(void *object) {{rhi_{kind}_state_destroy(({state}*)object);}}
static {state} *pilot_native_create_{kind}(const {desc} *key) {{
    volatile uint64_t *counts=pilot_native_pipeline_counters+{index*4};
    if(!pilot_native_pipeline_enabled()) {{++counts[0];++counts[2];return rhi_{kind}_state_create(key);}}
    return ({state}*)pilot_pipeline_cache_get(&pilot_native_{kind}_cache,key,sizeof(*key),
        pilot_native_make_{kind},pilot_native_free_{kind},counts);
}}
static void pilot_native_drop_{kind}({state} *object) {{if(!pilot_native_pipeline_enabled())rhi_{kind}_state_destroy(object);}}
'''
        variable={'blend':'g_blend_state','depth':'g_ds_state','raster':'g_raster_state'}[kind]
        old=f'rhi_{kind}_state_destroy({variable});'
        if code.count(old)!=2:raise ValueError('unreviewed pipeline retirement: '+kind)
        code=code.replace(old,f'pilot_native_drop_{kind}({variable});')
        argument={'blend':'bd','depth':'dsd','raster':'rd'}[kind]
        code=once(code,f'rhi_{kind}_state_create(&{argument})',f'pilot_native_create_{kind}(&{argument})')
    owner+='static void pilot_native_pipeline_shutdown(void) {\n'
    for kind in ('blend','depth','raster'):
        owner+=f'    pilot_pipeline_cache_clear(&pilot_native_{kind}_cache,pilot_native_free_{kind});\n'
    owner+='}\n'
    code=once(code,'    sampler_cache_shutdown();','    sampler_cache_shutdown();\n    pilot_native_pipeline_shutdown();')
    return once(code,'static RhiBlendState  *g_blend_state = NULL;',owner+'\nstatic RhiBlendState  *g_blend_state = NULL;')
