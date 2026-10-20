/* Renderer states use one semantic numbering scheme. Older XDK caches omit
 * later states; inline original code must still see its own compact layout.
 * An absent state has no guest storage and is an inactive capability (zero).
 * It can never be written through an admitted original API. */
static void graphics_render_map_check(void)
{
#ifdef PILOT_D3D_RENDER_STATE_MAP_VERSION
#if PILOT_D3D_GUEST_RENDER_STATE_COUNT < 1 || PILOT_D3D_GUEST_RENDER_STATE_COUNT > 166
#error Invalid original render-state count
#endif
    unsigned char seen[166]={0};
    for(unsigned state=0;state<166;++state) {
        int index=pilot_d3d_render_state_map[state];
        if(index<-1 || index>=(int)PILOT_D3D_GUEST_RENDER_STATE_COUNT || (state<57 && index!=(int)state))
            stop("invalid original render-state mapping",state);
        if(index>=0 && (seen[index]++ || pilot_d3d_render_state_inverse[index]!=state))
            stop("aliased original render-state mapping",state);
    }
    for(unsigned i=0;i<PILOT_D3D_GUEST_RENDER_STATE_COUNT;++i)
        if(!seen[i])stop("missing original render-state mapping",i);
#endif
}
static int graphics_render_guest_index(unsigned state)
{
    if(state>=166)stop("invalid canonical render state",state);
#ifdef PILOT_D3D_RENDER_STATE_MAP_VERSION
#if PILOT_D3D_RENDER_STATE_MAP_VERSION != 1
#error Unsupported render-state cache mapping ABI
#endif
    return pilot_d3d_render_state_map[state];
#else
    return (int)state;
#endif
}
static uint32_t graphics_render_read(unsigned state)
{
    int index=graphics_render_guest_index(state);
    return index<0?0:MEM32(PILOT_D3D_RENDER_STATES+4*(unsigned)index);
}
static void graphics_render_write(unsigned state,uint32_t value)
{
    int index=graphics_render_guest_index(state);
    if(index<0)stop("render state has no original cache slot",state);
    MEM32(PILOT_D3D_RENDER_STATES+4*(unsigned)index)=value;
}
static unsigned graphics_render_from_guest(unsigned index)
{
#ifdef PILOT_D3D_RENDER_STATE_MAP_VERSION
    if(index>=PILOT_D3D_GUEST_RENDER_STATE_COUNT)stop("invalid original render state",index);
    return pilot_d3d_render_state_inverse[index];
#else
    if(index>=166)stop("invalid original render state",index);
    return index;
#endif
}
static void graphics_render_copy(uint32_t values[166])
{
    for(unsigned state=0;state<166;++state)values[state]=graphics_render_read(state);
}
static int graphics_render_capture(FILE *file)
{
    uint32_t values[166];graphics_render_copy(values);
    return fwrite(values,sizeof(values),1,file)==1;
}
