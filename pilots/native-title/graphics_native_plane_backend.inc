/* Optional enhanced images never own guest-visible pixels or completion.
 * Logical image owners retain registrations across aliases and Reset. */
#ifdef PILOT_D3D_PHOBOS
static void pilot_native_attach(struct PilotImageUse *use,ID3D11Texture2D *image,unsigned width,unsigned height)
{
    if(!pilot_native_enabled() || !use || use->native_map || !image)return;
    D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(image,&desc);
    unsigned enhanced_width=0,enhanced_height=0;
    if(desc.MipLevels!=1 || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 ||
       !pilot_native_dimensions(width,height,desc.Width,desc.Height,&enhanced_width,&enhanced_height)) {
        fprintf(stderr,"[NATIVE PLANE] requested_axis=%u logical=%ux%u native=%ux%u rejected: source shape or enhanced dimension limit; native image retained\n",
            pilot_visual.supersample,width,height,desc.Width,desc.Height);
        pilot_native_degrade();return;
    }
    struct PilotNativePlane *plane=pilot_native_plane_create(image,enhanced_width,enhanced_height);
    if(plane){
        use->native_map=pilot_native_map_create(image,plane);
        /* A title may fill its color target without a preceding full clear.
         * Seed the whole companion from the original resident image BEFORE
         * its first native draw, then share every subsequent prepared draw.
         * This copies no pixels to/from the CPU and never changes native data. */
        if(use->native_map && plane->rtv)pilot_native_refresh_color(plane);
        pilot_native_plane_release(plane);
    }
    if(!use->native_map)pilot_native_degrade();
}
static void pilot_native_written(ID3D11Texture2D *image,int color)
{
    pilot_native_epoch_flush();
    if(!pilot_native_enabled())return;
    struct PilotNativePlane *plane=pilot_native_plane_find((ID3D11Resource*)image);
    if(!plane)return;
    plane->coherent=plane->valid_aspects=0;
    if(!color || !pilot_native_refresh_color(plane))pilot_native_degrade();
}
static void pilot_native_clear(ID3D11RenderTargetView *rtv,ID3D11DepthStencilView *dsv,
    unsigned flags,unsigned color,float depth,unsigned stencil,const D3D11_RECT *rect)
{
    pilot_native_epoch_flush();
    if(!pilot_native_enabled())return;
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
    /* Each attachment can have a larger extent. Clear its exact native
     * rectangle in its own enhanced grid without changing the canonical one. */
    for(unsigned kind=0;kind<2;++kind) {
        unsigned selected=flags&(kind?6:1);
        if(!selected || (kind?!dsv:!rtv))continue;
        ID3D11Resource *resource=NULL;
        if(kind)ID3D11DepthStencilView_GetResource(dsv,&resource);
        else ID3D11RenderTargetView_GetResource(rtv,&resource);
        struct PilotNativePlane *plane=pilot_native_plane_find(resource);ID3D11Resource_Release(resource);
        if(!plane){pilot_native_degrade();continue;}
        int full=!rect || (!rect->left && !rect->top &&
            rect->right==plane->native_width && rect->bottom==plane->native_height);
        if(full) {
            if(kind)ID3D11DeviceContext_ClearDepthStencilView(ctx,plane->dsv,
                ((selected&2)?1:0)|((selected&4)?2:0),depth,(UINT8)stencil);
            else {
                float rgba[]={((color>>16)&255)/255.0f,((color>>8)&255)/255.0f,(color&255)/255.0f,(color>>24)/255.0f};
                ID3D11DeviceContext_ClearRenderTargetView(ctx,plane->rtv,rgba);
            }
            plane->valid_aspects|=selected;
            unsigned all=1;
            if(kind) {
                D3D11_DEPTH_STENCIL_VIEW_DESC view;ID3D11DepthStencilView_GetDesc(plane->dsv,&view);
                all=view.Format==DXGI_FORMAT_D16_UNORM || view.Format==DXGI_FORMAT_D32_FLOAT?2:6;
            }
            plane->coherent=(plane->valid_aspects&all)==all;
        } else if((plane->valid_aspects&selected)==selected && !pilot_native_query_active()) {
            /* Private companions cannot be referenced by guest SRVs, and
             * admitted guest paths have no OM UAVs. See draw helper contract. */
            D3D11_RECT scaled;
            if(!pilot_native_draw_edge(rect->left,plane->width,plane->native_width,0,&scaled.left) ||
               !pilot_native_draw_edge(rect->right,plane->width,plane->native_width,1,&scaled.right) ||
               !pilot_native_draw_edge(rect->top,plane->height,plane->native_height,0,&scaled.top) ||
               !pilot_native_draw_edge(rect->bottom,plane->height,plane->native_height,1,&scaled.bottom)) {
                plane->coherent=plane->valid_aspects=0;pilot_native_degrade();continue;
            }
            struct PilotVisualState saved;pilot_visual_state_save(&saved);
            ID3D11RenderTargetView *output=kind?NULL:plane->rtv;
            ID3D11DeviceContext_OMSetRenderTargets(ctx,kind?0:1,&output,kind?plane->dsv:NULL);
            pilot_clear_rectangle(selected,color,depth,stencil,&scaled);
            pilot_visual_state_restore(&saved);
        } else {plane->coherent=plane->valid_aspects=0;pilot_native_degrade();}
    }
}
#else
static void pilot_native_clear(ID3D11RenderTargetView *r,ID3D11DepthStencilView *d,unsigned f,unsigned c,float z,unsigned s,const D3D11_RECT *rect)
{(void)r;(void)d;(void)f;(void)c;(void)z;(void)s;(void)rect;}
#endif
