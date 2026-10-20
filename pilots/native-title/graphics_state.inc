/* Guest arrays are part of the Xbox API: inline setters bypass named calls.
 * Simple packet setters change GPU state only; their callers maintain the
 * guest cache. Deferred arrays are sampled together at the draw boundary. */
static uint32_t graphics_state_baseline[166],graphics_texture_baseline[4][32];

static void graphics_render_state(uint32_t state,uint32_t value,int cache)
{
    /* The donor converts real packed pixels at upload. The original API's
     * enable/cache state gates that converted sampling path at draw time.
     * Disabled packed-YUV sampling remains unqualified, never mislabeled RGB. */
    if(state==PILOT_D3D_YUV_STATE){if(cache)MEM32(PILOT_D3D_RENDER_STATES+4*state)=value;return;}
    if(!pilot_gfx_xbox_render_state(state,value)) {
        fprintf(stderr,"[D3D STATE] state=%u value=%08x\n",state,value);
        stop("unqualified render-state value",state);
    }
    if(cache) {
        if(state==65 || state==76 || state==112 || state==117 || state==137 || state==142 || state==149) {
            if(!pilot_initializer_cache((uint32_t*)(memory+PILOT_D3D_RENDER_STATES),166,state,value))
                stop("invalid initializer cache update",state);
        } else MEM32(PILOT_D3D_RENDER_STATES+4*state)=value;
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
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET)=0;
    MEM32(PILOT_D3D_DEVICE_OBJECT+8)=0;
    for(unsigned i=0;i<109;++i) {
        uint32_t value=pilot_d3d_render_defaults[i];
        /* The reviewed original initializer explicitly skips these slots. */
        if(i!=0x184/4 && i!=0x114/4 && i!=0x118/4)
            MEM32(PILOT_D3D_RENDER_STATES+(i+57)*4)=value;
    }
    MEM32(PILOT_D3D_RENDER_STATES+143*4)=graphics_parameters[8]!=0;
    memcpy(graphics_state_baseline,memory+PILOT_D3D_RENDER_STATES,sizeof(graphics_state_baseline));
    static const uint32_t render[]={57,58,59,60,61,62,63,64,66,67,68,69,70,71,72,73,74,
        92,93,94,95,96,97,102,103,115,138,139,143,144,145,147,148,150};
    for(unsigned i=0;i<sizeof(render)/sizeof(*render);++i)
        graphics_render_state(render[i],graphics_state_baseline[render[i]],0);
    static const unsigned texture[]={0,1,2,3,4,5,6,7,8,12,14,15,16,18,19,20,22,23,24,25,26,27,28,29};
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
    if(!MEM32(PILOT_D3D_RENDER_STATES+PILOT_D3D_YUV_STATE*4))for(unsigned stage=0;stage<4;++stage) {
        uint32_t header=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+stage*4);
        if(header){uint32_t format=(MEM32(header+12)>>8)&255;
            if(format==0x24 || format==0x25)stop("packed YUV sampling requires qualified conversion state",header);}
    }
    for(unsigned state=92;state<136;++state) {
        uint32_t value=MEM32(PILOT_D3D_RENDER_STATES+4*state);
        if(PILOT_D3D_RENDER_STATES+4*state==PILOT_D3D_PRESENT_INTERVAL) {
            graphics_present_interval(value);continue; /* Applied by the actual presentation queue. */
        }
        if(!pilot_gfx_xbox_render_state(state,value) && value!=graphics_state_baseline[state])
            stop("changed unqualified deferred render state",state);
    }
    for(unsigned stage=0;stage<4;++stage) for(unsigned state=0;state<22;++state) {
        uint32_t value=MEM32(PILOT_D3D_TEXTURE_STATES+128*stage+4*state);
        if(!pilot_gfx_xbox_texture_state(stage,state,value) && value!=graphics_texture_baseline[stage][state]) {
            fprintf(stderr,"[D3D DEFERRED TEXTURE STATE] stage=%u state=%u value=%08x baseline=%08x pixel_shader=%08x\n",
                stage,state,value,graphics_texture_baseline[stage][state],
                MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER));
            stop("changed unqualified deferred texture state",state);
        }
    }
}
