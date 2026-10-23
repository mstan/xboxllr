/* Guest arrays are part of the Xbox API: inline setters bypass named calls.
 * Simple packet setters change GPU state only; their callers maintain the
 * guest cache. Deferred arrays are sampled together at the draw boundary. */
static uint32_t graphics_state_baseline[166],graphics_texture_baseline[4][32];
static struct {
    uint64_t revision[5];
    uint32_t render[44],texture[4][22];
    unsigned valid;
} graphics_deferred_cache;
/* Diagnostic baseline selection; ordinary launches keep the optimized path. */
static int graphics_deferred_cache_enabled=-1;
static int graphics_deferred_tracking(uint64_t revision[5])
{
    if(graphics_deferred_cache_enabled<0) {
        const char *value=getenv("XLLE_PILOT_DEFERRED_STATE_CACHE");
        graphics_deferred_cache_enabled=!(value && !strcmp(value,"0"));
    }
    return graphics_deferred_cache_enabled && pilot_gfx_deferred_revisions(revision);
}

/* These fields only feed fixed-function vertex lighting/material selection.
 * Preserve their actual guest cache even when a program or unlit draw makes
 * them inactive. A source-qualified lit draw samples these controls together
 * with the original material and enabled-light list before native submission. */
static int graphics_lighting_parameter(uint32_t state)
{return (state>=104 && state<=111) || state==113 || state==114;}
static int graphics_lighting_parameter_valid(uint32_t state,uint32_t value)
{return graphics_lighting_parameter(state) && (state==114 || value<=(state<=105?1u:2u));}
/* Keep the original point cache. Triangle draws do not consume it; the native
 * point owner validates active size/sprite/scale parameters before submission.
 * POINTSIZE_MIN (117) has its separately qualified initializer owner. */
static int graphics_point_parameter(uint32_t state)
{return state==116 || (state>=118 && state<=123);}
static int graphics_point_parameter_valid(uint32_t state,uint32_t value)
{return graphics_point_parameter(state) && ((state==118 || state==119)?value<=1:
    !(value&0x80000000u) && (value&0x7f800000u)!=0x7f800000u);}

static void graphics_render_state(uint32_t state,uint32_t value,int cache)
{
    if(graphics_lighting_parameter(state)) {
        if(!graphics_lighting_parameter_valid(state,value))stop("unqualified lighting parameter value",state);
        if(cache)graphics_render_write(state,value);
        return;
    }
    if(graphics_point_parameter(state)) {
        if(!graphics_point_parameter_valid(state,value))stop("unqualified point parameter value",state);
        if(cache)graphics_render_write(state,value);
        return;
    }
    /* The donor converts real packed pixels at upload. The original API's
     * enable/cache state gates that converted sampling path at draw time.
     * Disabled packed-YUV sampling remains unqualified, never mislabeled RGB. */
    if(state==PILOT_D3D_YUV_STATE){if(cache)graphics_render_write(state,value);return;}
    if(!pilot_gfx_xbox_render_state(state,value)) {
        fprintf(stderr,"[D3D STATE] state=%u value=%08x\n",state,value);
        stop("unqualified render-state value",state);
    }
    if(cache) {
        if(state==65 || state==76 || state==112 || state==117 || state==137 || state==142 || state==149) {
            uint32_t effects[166]={0};
            if(!pilot_initializer_cache(effects,166,state,value))
                stop("invalid initializer cache update",state);
            if(state==149)for(unsigned i=77;i<=81;++i)graphics_render_write(i,effects[i]);
        }
        graphics_render_write(state,value);
    }
}
static void graphics_texture_state(uint32_t stage,uint32_t state,uint32_t value,int cache)
{
    if(!pilot_gfx_xbox_texture_state(stage,state,value)) {
        fprintf(stderr,"[D3D TEXTURE STATE] stage=%u state=%u value=%08x\n",stage,state,value);
        stop("unqualified texture-state value",state);
    }
    if(cache)MEM32(PILOT_D3D_TEXTURE_STATES+128*stage+4*state)=value;
}
static void graphics_state_init(void)
{
    graphics_deferred_cache.valid=0;
    graphics_render_map_check();
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET)=0;
    MEM32(PILOT_D3D_DEVICE_OBJECT+8)=0;
#ifdef PILOT_D3D_RENDER_STATE_MAP_VERSION
    for(unsigned i=0;i<PILOT_D3D_RENDER_DEFAULT_COUNT;++i) {
        if(pilot_d3d_render_default_skips[i])continue;
        unsigned state=graphics_render_from_guest(i+57);
        graphics_render_write(state,pilot_d3d_render_defaults[i]);
    }
#else
    for(unsigned i=0;i<109;++i) {
        uint32_t value=pilot_d3d_render_defaults[i];
        /* The reviewed original initializer explicitly skips these slots. */
        if(i!=0x184/4 && i!=0x114/4 && i!=0x118/4)
            MEM32(PILOT_D3D_RENDER_STATES+(i+57)*4)=value;
    }
#endif
    graphics_render_presentation_cache(graphics_parameters[4]);
    graphics_render_write(143,graphics_parameters[8]!=0);
    graphics_render_copy(graphics_state_baseline);
    static const uint32_t render[]={57,58,59,60,61,62,63,64,66,67,68,69,70,71,72,73,74,
        92,93,94,95,96,97,102,103,115,138,139,143,144,145,147,148,150};
    for(unsigned i=0;i<sizeof(render)/sizeof(*render);++i)
        graphics_render_state(render[i],graphics_state_baseline[render[i]],0);
    static const unsigned texture[]={0,1,2,3,4,5,6,7,8,12,14,15,16,18,19,20,
#ifdef PILOT_D3D_PHOBOS
        21,
#endif
        22,23,24,25,26,27,28,29};
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t *values=graphics_texture_baseline[stage];
        memcpy(values,pilot_d3d_texture_defaults,sizeof(graphics_texture_baseline[stage]));
        values[28]=stage;
        if(!stage){values[12]=4;values[16]=2;}
        memcpy(memory+PILOT_D3D_TEXTURE_STATES+stage*128,values,128);
        for(unsigned i=0;i<sizeof(texture)/sizeof(*texture);++i)
            graphics_texture_state(stage,texture[i],values[texture[i]],0);
    }
}
static void graphics_simple_state(uint32_t method,uint32_t value)
{
    uint32_t state;
    if(!pilot_graphics_simple_decode(method,&state))stop("unqualified simple graphics packet",method);
    graphics_render_state(state,value,0);
}
static void graphics_sync_deferred(void)
{
    if(!graphics_render_read(PILOT_D3D_YUV_STATE))for(unsigned stage=0;stage<4;++stage) {
        uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+stage*4);
        if(header){uint32_t format=(MEM32(header+12)>>8)&255;
            if(format==0x24 || format==0x25)stop("packed YUV sampling requires qualified conversion state",header);}
    }
    /* Inline setters can bypass every named entry point: compare the actual
     * guest words on every draw. A native packet/setter/reset may also change
     * the renderer while those words stay unchanged, so require its audited
     * revision too. Neither path consumes guest dirty bits or skips draws. */
    uint32_t render[44],texture[4][22];
    for(unsigned i=0;i<44;++i)render[i]=graphics_render_read(92+i);
    for(unsigned stage=0;stage<4;++stage)
        memcpy(texture[stage],memory+PILOT_D3D_TEXTURE_STATES+128*stage,sizeof(texture[stage]));
    uint64_t revision[5];
    int tracked=graphics_deferred_tracking(revision);
    unsigned changed=0;
    if(!tracked || !(graphics_deferred_cache.valid&1) ||
       graphics_deferred_cache.revision[0]!=revision[0] ||
       memcmp(render,graphics_deferred_cache.render,sizeof(render)))changed|=1;
    for(unsigned stage=0;stage<4;++stage)
        if(!tracked || !(graphics_deferred_cache.valid&(2u<<stage)) ||
           graphics_deferred_cache.revision[stage+1]!=revision[stage+1] ||
           memcmp(texture[stage],graphics_deferred_cache.texture[stage],sizeof(texture[stage])))
            changed|=2u<<stage;
    if(!changed)return;
    if(changed&1)
    for(unsigned state=92;state<136;++state) {
        int index=graphics_render_guest_index(state);if(index<0)continue;
        uint32_t value=render[state-92];
        if(graphics_lighting_parameter(state)) {
            if(!graphics_lighting_parameter_valid(state,value))stop("unqualified deferred lighting parameter",state);
            continue;
        }
        if(graphics_point_parameter(state)) {
            if(!graphics_point_parameter_valid(state,value))stop("unqualified deferred point parameter",state);
            continue;
        }
        if(PILOT_D3D_RENDER_STATES+4*(unsigned)index==PILOT_D3D_PRESENT_INTERVAL) {
            graphics_present_interval(value);continue; /* Applied by the actual presentation queue. */
        }
        if(!pilot_gfx_xbox_render_state(state,value) && value!=graphics_state_baseline[state]) {
            fprintf(stderr,"[D3D DEFERRED RENDER STATE] state=%u value=%08x baseline=%08x lighting=%u shader=%08x\n",
                state,value,graphics_state_baseline[state],graphics_render_read(102),
                MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER));
            stop("changed unqualified deferred render state",state);
        }
    }
    for(unsigned stage=0;stage<4;++stage) if(changed&(2u<<stage)) for(unsigned state=0;state<22;++state) {
        uint32_t value=texture[stage][state];
        if(!pilot_gfx_xbox_texture_state(stage,state,value) && value!=graphics_texture_baseline[stage][state]) {
            fprintf(stderr,"[D3D DEFERRED TEXTURE STATE] stage=%u state=%u value=%08x baseline=%08x pixel_shader=%08x\n",
                stage,state,value,graphics_texture_baseline[stage][state],
                MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER));
            stop("changed unqualified deferred texture state",state);
        }
    }
    memcpy(graphics_deferred_cache.render,render,sizeof(render));
    memcpy(graphics_deferred_cache.texture,texture,sizeof(texture));
    graphics_deferred_cache.valid=graphics_deferred_tracking(graphics_deferred_cache.revision)?31u:0u;
}
