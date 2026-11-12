/* R09 submission: original donor images and the FIRST raw draw are canonical.
 * An optional enhanced companion consumes the same prepared GPU IA/shaders;
 * geometry/state preparation is never invoked twice. Include after the native
 * plane header/resources. The graphics lock owns the entire operation.
 *
 * begin() is called AFTER the original native draw. S_OK binds an admitted
 * enhanced pass; S_FALSE leaves the context untouched and needs no extra draw.
 * end() restores captured OM, PS0..4, viewport/scissor state and releases refs.
 * Donor UP must retain its already prepared GPU IA data for that optional pass;
 * it must not invoke the donor UP preparer again. invalidate() lets an UP caller
 * reject a begun pass before end() when it cannot retain that prepared IA.
 */
#ifdef PILOT_D3D_PHOBOS
/* Closed binding contract: companions never escape these helpers. Enhanced
 * SRVs occupy only replay PS0..4 or private presentation/refresh bindings,
 * which restore their state. Guest bindings cannot alias an enhanced output;
 * admitted native draw paths have no OM UAVs. Any new companion binding route
 * must preserve this invariant or extend the state capture here. */
enum { PILOT_NATIVE_DRAW_SRVS=5 };
struct PilotNativeDrawWrites {
    unsigned color[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT],depth,depth_all,depth_required,any;
};
struct PilotNativeDrawState {
    ID3D11DeviceContext *context;
    ID3D11Query *excluded_query; /* Borrowed from the guest visibility counter. */
    ID3D11RenderTargetView *outputs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT],*enhanced_color;
    ID3D11DepthStencilView *depth,*enhanced_depth;
    ID3D11DepthStencilState *prepared_depth;
    ID3D11BlendState *prepared_blend;
    D3D11_BLEND_DESC prepared_blend_desc;
    D3D11_DEPTH_STENCIL_DESC prepared_depth_desc;
    ID3D11ShaderResourceView *inputs[PILOT_NATIVE_DRAW_SRVS],*enhanced_inputs[PILOT_NATIVE_DRAW_SRVS];
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D11_VIEWPORT enhanced_viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D11_RECT enhanced_scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    struct PilotNativePlane *color_plane,*depth_plane;
    struct PilotNativeDrawWrites writes;
    UINT viewport_count,scissor_count;
    unsigned bound,enhanced,depth_required,writes_known,binding_reuse,input_remap,borrowed_inputs,prepared_bindings,borrowed_bindings;
    DXGI_FORMAT depth_format;
};

static int pilot_native_draw_binding_reuse(void)
{
    static int enabled=-1;
    if(enabled<0) {
        const char *value=getenv("XLLE_PILOT_NATIVE_BINDING_REUSE");
        enabled=value && !strcmp(value,"1");
    }
    return enabled;
}
enum PilotNativeDrawReason {
    PILOT_NATIVE_REASON_NO_OUTPUT,PILOT_NATIVE_REASON_MISSING_COLOR,PILOT_NATIVE_REASON_MISSING_DEPTH,
    PILOT_NATIVE_REASON_QUERY,PILOT_NATIVE_REASON_COLOR_MASK,PILOT_NATIVE_REASON_DEPTH_MASK,
    PILOT_NATIVE_REASON_COLOR_SHAPE,PILOT_NATIVE_REASON_COLOR_VIEW,PILOT_NATIVE_REASON_DEPTH_SHAPE,
    PILOT_NATIVE_REASON_DEPTH_VIEW,PILOT_NATIVE_REASON_NATIVE_DEPTH_SIZE,PILOT_NATIVE_REASON_HD_DEPTH_SIZE,
    PILOT_NATIVE_REASON_GRID,PILOT_NATIVE_REASON_INPUT_MASK,PILOT_NATIVE_REASON_INPUT_VIEW,
    PILOT_NATIVE_REASON_INPUT_NULL,PILOT_NATIVE_REASON_ALIAS,PILOT_NATIVE_REASON_SCISSOR,PILOT_NATIVE_REASON_MRT
};
static void pilot_native_draw_diagnostic(struct PilotNativeDrawState *draw,enum PilotNativeDrawReason reason,
    int stage,struct PilotNativePlane *input,unsigned required,HRESULT result)
{
    static int enabled=-1;static uint64_t reported;
    if(enabled<0) {
        const char *value=getenv("XLLE_PILOT_NATIVE_RENDER_DIAGNOSTICS");enabled=value && !strcmp(value,"1");
    }
    uint64_t bit=UINT64_C(1)<<(unsigned)reason;
    if(!enabled || (reported&bit))return;
    reported|=bit;
    static const char *const names[]={"no-paired-output","missing-color-pair","missing-depth-pair",
        "visibility-exclusion-unavailable","color-valid-mask","depth-required-mask","color-shape","color-view-contract",
        "depth-shape","depth-view-contract","native-depth-coverage","enhanced-depth-coverage",
        "depth-color-grid-mismatch","input-valid-mask","input-view-failure","input-view-null",
        "input-output-alias","scissor-overflow","unsupported-mrt"};
    struct PilotNativePlane *color=draw->color_plane,*depth=draw->depth_plane;
    unsigned native[2][2]={{color?color->native_width:0,color?color->native_height:0},
        {depth?depth->native_width:0,depth?depth->native_height:0}};
    /* Missing-map diagnostics still identify the actual original output grid.
     * Descriptor/ref queries occur only on this first logged occurrence. */
    for(unsigned i=0;i<2;++i)if(!native[i][0]) {
        ID3D11Resource *resource=NULL;ID3D11Texture2D *image=NULL;
        if(i==0 && draw->outputs[0])ID3D11RenderTargetView_GetResource(draw->outputs[0],&resource);
        if(i==1 && draw->depth)ID3D11DepthStencilView_GetResource(draw->depth,&resource);
        if(resource) {
            if(SUCCEEDED(ID3D11Resource_QueryInterface(resource,&IID_ID3D11Texture2D,(void**)&image))) {
                D3D11_TEXTURE2D_DESC shape;ID3D11Texture2D_GetDesc(image,&shape);
                native[i][0]=shape.Width;native[i][1]=shape.Height;ID3D11Texture2D_Release(image);
            }
            ID3D11Resource_Release(resource);
        }
    }
    fprintf(stderr,"[NATIVE DRAW SKIP] reason=%s stage=%d result=%08lx color_native=%ux%u color_hd=%ux%u color_mask=%u color_coherent=%u depth_native=%ux%u depth_hd=%ux%u depth_mask=%u depth_required=%u query=%u input_native=%ux%u input_hd=%ux%u input_mask=%u input_required=%u\n",
        names[(unsigned)reason],stage,(unsigned long)(uint32_t)result,native[0][0],native[0][1],
        color?color->width:0,color?color->height:0,color?color->valid_aspects:0,color?color->coherent:0,
        native[1][0],native[1][1],depth?depth->width:0,depth?depth->height:0,depth?depth->valid_aspects:0,
        draw->depth_required,(unsigned)pilot_native_query_active(),input?input->native_width:0,input?input->native_height:0,
        input?input->width:0,input?input->height:0,input?input->valid_aspects:0,required);
}

static void pilot_native_draw_release(struct PilotNativeDrawState *draw)
{
    for(unsigned i=0;i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++i)
        if(draw->outputs[i] && !draw->borrowed_bindings)ID3D11RenderTargetView_Release(draw->outputs[i]);
    if(draw->enhanced_color)ID3D11RenderTargetView_Release(draw->enhanced_color);
    if(draw->depth && !draw->borrowed_bindings)ID3D11DepthStencilView_Release(draw->depth);
    if(draw->prepared_depth && !draw->borrowed_bindings)ID3D11DepthStencilState_Release(draw->prepared_depth);
    if(draw->prepared_blend && !draw->borrowed_bindings)ID3D11BlendState_Release(draw->prepared_blend);
    if(draw->enhanced_depth)ID3D11DepthStencilView_Release(draw->enhanced_depth);
    for(unsigned i=0;i<PILOT_NATIVE_DRAW_SRVS;++i) {
        if(draw->inputs[i] && !draw->borrowed_bindings)ID3D11ShaderResourceView_Release(draw->inputs[i]);
        if(draw->enhanced_inputs[i] && !(draw->borrowed_inputs&(1u<<i)))ID3D11ShaderResourceView_Release(draw->enhanced_inputs[i]);
    }
    memset(draw,0,sizeof(*draw));
}
static int pilot_native_draw_forget(struct PilotNativePlane *plane,unsigned written,unsigned all)
{
    if(!plane || !written)return 0;
    unsigned before=plane->valid_aspects,coherent=plane->coherent;
    plane->valid_aspects&=~written;
    plane->coherent=(plane->valid_aspects&all)==all;
    return before!=plane->valid_aspects || coherent!=plane->coherent;
}
static const struct PilotNativeDrawWrites *pilot_native_draw_write_masks(struct PilotNativeDrawState *draw)
{
    if(draw->writes_known)return &draw->writes;
    draw->writes_known=1;
    /* Only skipped replay or active guest queries inspect these descriptors.
     * Cache them so a declined query replay does not inspect OM state twice. */
    D3D11_BLEND_DESC blend={0};ID3D11BlendState *blend_state=NULL;
    blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(draw->prepared_bindings)blend_state=draw->prepared_blend;
    else ID3D11DeviceContext_OMGetBlendState(draw->context,&blend_state,NULL,NULL);
    if(blend_state) {
        if(draw->prepared_bindings)blend=draw->prepared_blend_desc;
        else pilot_native_admission_blend_desc(blend_state,&blend);
        if(!draw->prepared_bindings)ID3D11BlendState_Release(blend_state);
    }
    for(unsigned i=0;i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++i)if(draw->outputs[i]) {
        unsigned slot=blend.IndependentBlendEnable?i:0;
        draw->writes.color[i]=blend.RenderTarget[slot].RenderTargetWriteMask?1u:0u;
        draw->writes.any|=draw->writes.color[i];
    }
    if(draw->depth) {
        D3D11_DEPTH_STENCIL_DESC depth={0};ID3D11DepthStencilState *depth_state=NULL;UINT reference=0;
        /* NULL D3D11 depth state: depth enabled/writeALL, stencil disabled. */
        depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
        if(draw->prepared_bindings)depth_state=draw->prepared_depth;
        else ID3D11DeviceContext_OMGetDepthStencilState(draw->context,&depth_state,&reference);
        if(depth_state) {
            if(draw->prepared_bindings)depth=draw->prepared_depth_desc;
            else pilot_native_admission_depth_desc(depth_state,&depth);
            if(!draw->prepared_bindings)ID3D11DepthStencilState_Release(depth_state);
        }
        draw->writes.depth_required=(depth.DepthEnable?2u:0u)|(depth.StencilEnable?4u:0u);
        draw->writes.depth=depth.DepthEnable && depth.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL?2u:0u;
        if(depth.StencilEnable && depth.StencilWriteMask &&
           (depth.FrontFace.StencilFailOp!=D3D11_STENCIL_OP_KEEP || depth.FrontFace.StencilDepthFailOp!=D3D11_STENCIL_OP_KEEP ||
            depth.FrontFace.StencilPassOp!=D3D11_STENCIL_OP_KEEP || depth.BackFace.StencilFailOp!=D3D11_STENCIL_OP_KEEP ||
            depth.BackFace.StencilDepthFailOp!=D3D11_STENCIL_OP_KEEP || depth.BackFace.StencilPassOp!=D3D11_STENCIL_OP_KEEP))draw->writes.depth|=4;
        /* Read-only DSV flags can only make this more conservative. D16/D32
         * have no stencil aspect; D24S8/D32S8 require both format aspects. */
        DXGI_FORMAT format=draw->depth_format;
        if(format==DXGI_FORMAT_UNKNOWN) {D3D11_DEPTH_STENCIL_VIEW_DESC view;ID3D11DepthStencilView_GetDesc(draw->depth,&view);format=view.Format;}
        draw->writes.depth_all=format==DXGI_FORMAT_D16_UNORM || format==DXGI_FORMAT_D32_FLOAT?2u:6u;
        draw->writes.any|=draw->writes.depth;
    }
    return &draw->writes;
}
static void pilot_native_draw_invalidate(struct PilotNativeDrawState *draw)
{
    const struct PilotNativeDrawWrites *writes=pilot_native_draw_write_masks(draw);
    unsigned invalidated=0;
    for(unsigned i=0;i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++i)if(draw->outputs[i] && writes->color[i]) {
        struct PilotNativePlane *plane=draw->color_plane;
        if(i) {
            ID3D11Resource *resource=NULL;
            ID3D11RenderTargetView_GetResource(draw->outputs[i],&resource);
            plane=pilot_native_plane_find(resource);ID3D11Resource_Release(resource);
        }
        invalidated|=(unsigned)pilot_native_draw_forget(plane,1,1);
    }
    if(draw->depth_plane && draw->depth)
        invalidated|=(unsigned)pilot_native_draw_forget(draw->depth_plane,writes->depth,writes->depth_all);
    draw->enhanced=0;if(invalidated)pilot_native_degrade();
}

static HRESULT pilot_native_draw_shape(ID3D11Resource *resource,D3D11_TEXTURE2D_DESC *shape)
{
    ID3D11Texture2D *image=NULL;
    HRESULT result=ID3D11Resource_QueryInterface(resource,&IID_ID3D11Texture2D,(void**)&image);
    if(FAILED(result))return result;
    ID3D11Texture2D_GetDesc(image,shape);ID3D11Texture2D_Release(image);
    /* Existing renderable views are whole level-zero single-image 2D views. */
    if(!shape->Width || !shape->Height || shape->ArraySize!=1 || shape->MipLevels!=1)return E_NOTIMPL;
    return S_OK;
}
static int pilot_native_draw_color_view(ID3D11RenderTargetView *original,ID3D11RenderTargetView *enhanced)
{
    D3D11_RENDER_TARGET_VIEW_DESC a={0},b={0};
    ID3D11RenderTargetView_GetDesc(original,&a);ID3D11RenderTargetView_GetDesc(enhanced,&b);
    if(a.Format!=b.Format || a.ViewDimension!=b.ViewDimension)return 0;
    return a.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DMS ||
        (a.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D && !a.Texture2D.MipSlice && !b.Texture2D.MipSlice);
}
static int pilot_native_draw_depth_view(ID3D11DepthStencilView *original,ID3D11DepthStencilView *enhanced)
{
    D3D11_DEPTH_STENCIL_VIEW_DESC a={0},b={0};
    ID3D11DepthStencilView_GetDesc(original,&a);ID3D11DepthStencilView_GetDesc(enhanced,&b);
    if(a.Format!=b.Format || a.ViewDimension!=b.ViewDimension || a.Flags!=b.Flags)return 0;
    return a.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DMS ||
        (a.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D && !a.Texture2D.MipSlice && !b.Texture2D.MipSlice);
}
static unsigned pilot_native_draw_depth_aspects(struct PilotNativeDrawState *draw)
{
    ID3D11DepthStencilState *state=NULL;UINT reference=0;
    if(draw->prepared_bindings)state=draw->prepared_depth;
    else ID3D11DeviceContext_OMGetDepthStencilState(draw->context,&state,&reference);
    /* D3D11's NULL state enables depth and disables stencil. No dynamic
     * stencil reference or actual bound state is changed by this inspection. */
    if(!state)return 2;
    D3D11_DEPTH_STENCIL_DESC desc;
    if(draw->prepared_bindings)desc=draw->prepared_depth_desc;
    else pilot_native_admission_depth_desc(state,&desc);
    if(!draw->prepared_bindings)ID3D11DepthStencilState_Release(state);
    return (desc.DepthEnable?2u:0u)|(desc.StencilEnable?4u:0u);
}
static unsigned pilot_native_draw_input_format(struct PilotNativePlane *plane,DXGI_FORMAT format)
{
    switch(format) {
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:return 2;
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:return 4;
    case DXGI_FORMAT_R16_UNORM:case DXGI_FORMAT_R32_FLOAT:
        /* A depth-only format's scratch copy can be SRV-only. Its transferred
         * aspect mask retains that identity even without a DSV binding. */
        if(plane->dsv || ((plane->valid_aspects&6u) && !(plane->valid_aspects&1u)))return 2;
        break;
    default:break;
    }
    return 1;
}
static unsigned pilot_native_draw_input_aspects(struct PilotNativePlane *plane,ID3D11ShaderResourceView *view)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC desc;ID3D11ShaderResourceView_GetDesc(view,&desc);
    return pilot_native_draw_input_format(plane,desc.Format);
}
static int pilot_native_draw_edge(LONG edge,UINT enhanced,UINT native,int upper,LONG *output)
{
    /* Scale the original native half-open bound outward. Canonical scissor
     * coordinates are retained unchanged, even at nonintegral HD ratios. */
    int64_t numerator=(int64_t)edge*enhanced;
    int64_t value=numerator/native,remainder=numerator%native;
    if(remainder && upper && numerator>0)++value;
    if(remainder && !upper && numerator<0)--value;
    if(value<INT32_MIN || value>INT32_MAX)return 0;
    *output=(LONG)value;return 1;
}

static HRESULT pilot_native_draw_prepare(struct PilotNativeDrawState *draw,int immediate)
{
    memset(draw,0,sizeof(*draw));
    if(!pilot_native_enabled())return S_FALSE;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    if(!context)return E_FAIL;
    draw->context=context;
    struct PreparedDrawSubset prepared={0};
    int captured=immediate?prepared_state_draw_subset(context,&prepared):prepared_state_draw_subset_borrow(context,&prepared);
    if(captured && !prepared.unsupported) {
        memcpy(draw->outputs,prepared.outputs,sizeof(draw->outputs));draw->depth=prepared.depth;
        memcpy(draw->inputs,prepared.inputs,sizeof(draw->inputs));
        draw->prepared_blend=prepared.blend;draw->prepared_depth=prepared.depth_state;
        draw->prepared_blend_desc=prepared.blend_desc;draw->prepared_depth_desc=prepared.depth_desc;
        memcpy(draw->viewports,prepared.viewports,sizeof(draw->viewports));
        memcpy(draw->scissors,prepared.scissors,sizeof(draw->scissors));
        draw->viewport_count=prepared.viewport_count;draw->scissor_count=prepared.scissor_count;
        draw->prepared_bindings=1;draw->borrowed_bindings=prepared.borrowed;
        memset(&prepared,0,sizeof(prepared)); /* Move owned refs or the bounded borrow. */
    } else {
        prepared_state_draw_release(&prepared);
        ID3D11DeviceContext_OMGetRenderTargets(context,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,draw->outputs,&draw->depth);
        ID3D11DeviceContext_PSGetShaderResources(context,0,PILOT_NATIVE_DRAW_SRVS,draw->inputs);
        draw->viewport_count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        draw->scissor_count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        ID3D11DeviceContext_RSGetViewports(context,&draw->viewport_count,draw->viewports);
        ID3D11DeviceContext_RSGetScissorRects(context,&draw->scissor_count,draw->scissors);
    }
    HRESULT result=E_NOTIMPL;
    int query_active=pilot_native_query_active();
    ID3D11Resource *color_resource=NULL,*depth_resource=NULL;
    unsigned color_owned=0,depth_owned=0;
    int immutable=pilot_native_admission_enabled();
    struct PilotNativeAdmissionView *color_facts=NULL,*depth_facts=NULL;
    D3D11_TEXTURE2D_DESC color_shape={0},depth_shape={0};
    UINT native_width=0,native_height=0,enhanced_width=0,enhanced_height=0;
    if(draw->outputs[0]) {
        if(immutable)color_facts=pilot_native_admission_get((ID3D11View*)draw->outputs[0],PILOT_NATIVE_ADMISSION_RTV,&color_resource,&color_owned);
        else {ID3D11RenderTargetView_GetResource(draw->outputs[0],&color_resource);color_owned=1;}
        draw->color_plane=color_facts?color_facts->owner->plane:pilot_native_plane_find(color_resource);
    }
    if(draw->depth) {
        if(immutable)depth_facts=pilot_native_admission_get((ID3D11View*)draw->depth,PILOT_NATIVE_ADMISSION_DSV,&depth_resource,&depth_owned);
        else {ID3D11DepthStencilView_GetResource(draw->depth,&depth_resource);depth_owned=1;}
        draw->depth_plane=depth_facts?depth_facts->owner->plane:pilot_native_plane_find(depth_resource);
        if(depth_facts)draw->depth_format=depth_facts->desc.dsv.Format;
        draw->depth_required=query_active?pilot_native_draw_write_masks(draw)->depth_required:pilot_native_draw_depth_aspects(draw);
    }
    /* A visibility-only draw has already contributed its native samples and
     * cannot change either image. No extra draw or exclusion query is needed. */
    if(query_active && !pilot_native_draw_write_masks(draw)->any) {result=S_FALSE;goto finished;}
    /* An unpaired output is already native. Remapped sampled inputs alone
     * must NEVER cause a second blend/depth write into that same image. */
    for(unsigned i=1;i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++i)
        if(draw->outputs[i]) {
            pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_MRT,(int)i,NULL,0,result);goto invalid;
        }
    if(!draw->color_plane && !(draw->depth_plane && draw->depth_required)) {
        result=S_FALSE;pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_NO_OUTPUT,-1,NULL,0,result);goto finished;
    }
    /* Every present writable attachment must have separate enhanced storage.
     * Incoherent suppression does no resource allocation or binding. */
    if(draw->outputs[0] && !draw->color_plane) {
        pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_MISSING_COLOR,-1,NULL,1,S_FALSE);goto skipped;
    }
    if(draw->depth_required && !draw->depth_plane) {
        pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_MISSING_DEPTH,-1,NULL,draw->depth_required,S_FALSE);goto skipped;
    }
    if(draw->color_plane && !(draw->color_plane->valid_aspects&1u)) {
        pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_COLOR_MASK,-1,NULL,1,S_FALSE);goto skipped;
    }
    if(draw->depth_plane && (draw->depth_plane->valid_aspects&draw->depth_required)!=draw->depth_required) {
        pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_DEPTH_MASK,-1,NULL,draw->depth_required,S_FALSE);goto skipped;
    }
    if(draw->color_plane) {
        struct PilotNativePlane *plane=draw->color_plane;
        if(color_facts){color_shape=color_facts->shape;result=color_facts->shape_result;}
        else result=pilot_native_draw_shape(color_resource,&color_shape);
        if(FAILED(result)) {
            pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_COLOR_SHAPE,-1,NULL,0,result);goto invalid;
        }
        if(!plane->image || !plane->enhanced || plane->image==plane->enhanced || !plane->rtv ||
           plane->native_width!=color_shape.Width || plane->native_height!=color_shape.Height ||
           !plane->width || !plane->height || !(color_facts?color_facts->compatible:pilot_native_draw_color_view(draw->outputs[0],plane->rtv))) {
            result=E_UNEXPECTED;pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_COLOR_VIEW,-1,NULL,0,result);goto invalid;
        }
        native_width=plane->native_width;native_height=plane->native_height;
        enhanced_width=plane->width;enhanced_height=plane->height;
        draw->enhanced_color=plane->rtv;ID3D11RenderTargetView_AddRef(draw->enhanced_color);
    }
    /* A disabled depth/stencil state cannot read or write this attachment.
     * Omit it so an inactive larger native grid cannot veto a color replay. */
    if(draw->depth_plane && draw->depth_required) {
        struct PilotNativePlane *plane=draw->depth_plane;
        if(depth_facts){depth_shape=depth_facts->shape;result=depth_facts->shape_result;}
        else result=pilot_native_draw_shape(depth_resource,&depth_shape);
        if(FAILED(result)) {
            pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_DEPTH_SHAPE,-1,NULL,0,result);goto invalid;
        }
        if(!plane->image || !plane->enhanced || plane->image==plane->enhanced || !plane->dsv ||
           plane->native_width!=depth_shape.Width || plane->native_height!=depth_shape.Height ||
           !plane->width || !plane->height || !(depth_facts?depth_facts->compatible:pilot_native_draw_depth_view(draw->depth,plane->dsv))) {
            result=E_UNEXPECTED;pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_DEPTH_VIEW,-1,NULL,0,result);goto invalid;
        }
        draw->enhanced_depth=plane->dsv;ID3D11DepthStencilView_AddRef(draw->enhanced_depth);
        if(!draw->outputs[0]) {
            native_width=plane->native_width;native_height=plane->native_height;
            enhanced_width=plane->width;enhanced_height=plane->height;
        } else {
            /* Larger native depth is legal. Color always owns coordinates. A
             * separately bounded HD depth allocation may only be a fallback. */
            if(plane->native_width<native_width || plane->native_height<native_height) {
                result=E_UNEXPECTED;pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_NATIVE_DEPTH_SIZE,-1,NULL,0,result);goto invalid;
            }
            if(plane->width<enhanced_width || plane->height<enhanced_height) {
                pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_HD_DEPTH_SIZE,-1,NULL,0,S_FALSE);goto skipped;
            }
            if((uint64_t)plane->width*native_width!=(uint64_t)enhanced_width*plane->native_width ||
               (uint64_t)plane->height*native_height!=(uint64_t)enhanced_height*plane->native_height) {
                pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_GRID,-1,NULL,0,S_FALSE);goto skipped;
            }
        }
    }
    for(unsigned i=0;i<PILOT_NATIVE_DRAW_SRVS;++i)if(draw->inputs[i]) {
        if(immutable) {
            ID3D11Resource *source=NULL;unsigned owned=0;
            struct PilotNativeAdmissionView *facts=pilot_native_admission_get((ID3D11View*)draw->inputs[i],
                PILOT_NATIVE_ADMISSION_SRV,&source,&owned);
            struct PilotNativePlane *plane=facts?facts->owner->plane:pilot_native_plane_find(source);
            /* The captured input view itself retains this resource. */
            if(owned)ID3D11Resource_Release(source);
            ID3D11Resource *sampled=source;
            if(plane) {
                unsigned required=facts?pilot_native_draw_input_format(plane,facts->desc.srv.Format):
                    pilot_native_draw_input_aspects(plane,draw->inputs[i]);
                if((plane->valid_aspects&required)!=required) {
                    pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_INPUT_MASK,(int)i,plane,required,S_FALSE);goto skipped;
                }
                if(facts) {
                    result=pilot_native_admission_srv(facts,&draw->enhanced_inputs[i]);
                    if(SUCCEEDED(result))draw->borrowed_inputs|=1u<<i;
                } else result=pilot_native_plane_view(plane,draw->inputs[i],&draw->enhanced_inputs[i]);
                if(FAILED(result)) {
                    pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_INPUT_VIEW,(int)i,plane,required,result);goto skipped;
                }
                if(!draw->enhanced_inputs[i]) {
                    result=E_UNEXPECTED;pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_INPUT_NULL,(int)i,plane,required,result);goto invalid;
                }
                /* The factory/cache constructs this view over this exact
                 * immutable companion, so its resource need not be re-queried. */
                sampled=(ID3D11Resource*)plane->enhanced;
            } else {
                draw->enhanced_inputs[i]=draw->inputs[i];draw->borrowed_inputs|=1u<<i;
            }
            int aliases=(draw->color_plane && sampled==(ID3D11Resource*)draw->color_plane->enhanced) ||
                (draw->enhanced_depth && sampled==(ID3D11Resource*)draw->depth_plane->enhanced);
            if(aliases) {
                pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_ALIAS,(int)i,plane,0,S_FALSE);goto skipped;
            }
            continue;
        }
        ID3D11Resource *resource=NULL;
        ID3D11ShaderResourceView_GetResource(draw->inputs[i],&resource);
        struct PilotNativePlane *plane=pilot_native_plane_find(resource);
        ID3D11Resource_Release(resource);
        if(plane) {
            unsigned required=pilot_native_draw_input_aspects(plane,draw->inputs[i]);
            if((plane->valid_aspects&required)!=required) {
                pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_INPUT_MASK,(int)i,plane,required,S_FALSE);goto skipped;
            }
            /* An optional HD SRV allocation failure must not jeopardize the
             * native draw which already completed submission. */
            result=pilot_native_plane_view(plane,draw->inputs[i],&draw->enhanced_inputs[i]);
            if(FAILED(result)) {
                pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_INPUT_VIEW,(int)i,plane,required,result);goto skipped;
            }
            if(!draw->enhanced_inputs[i]) {
                result=E_UNEXPECTED;pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_INPUT_NULL,(int)i,plane,required,result);goto invalid;
            }
        } else {
            draw->enhanced_inputs[i]=draw->inputs[i];ID3D11ShaderResourceView_AddRef(draw->enhanced_inputs[i]);
        }
        ID3D11ShaderResourceView_GetResource(draw->enhanced_inputs[i],&resource);
        int aliases=(draw->color_plane && resource==(ID3D11Resource*)draw->color_plane->enhanced) ||
            (draw->enhanced_depth && resource==(ID3D11Resource*)draw->depth_plane->enhanced);
        ID3D11Resource_Release(resource);
        /* Feedback and blend snapshots need their own coherent companion.
         * Avoid D3D11's silent NULL SRV substitution for an aliased output. */
        if(aliases) {
            pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_ALIAS,(int)i,plane,0,S_FALSE);goto skipped;
        }
    }
    draw->binding_reuse=(unsigned)pilot_native_draw_binding_reuse();
    for(unsigned i=0;i<PILOT_NATIVE_DRAW_SRVS;++i)
        draw->input_remap|=draw->inputs[i]!=draw->enhanced_inputs[i];
    D3D11_VIEWPORT *viewports=draw->enhanced_viewports;
    D3D11_RECT *scissors=draw->enhanced_scissors;
    memcpy(viewports,draw->viewports,sizeof(draw->viewports));memcpy(scissors,draw->scissors,sizeof(draw->scissors));
    for(UINT i=0;i<draw->viewport_count;++i) {
        viewports[i].TopLeftX=(float)((double)viewports[i].TopLeftX*enhanced_width/native_width);
        viewports[i].TopLeftY=(float)((double)viewports[i].TopLeftY*enhanced_height/native_height);
        viewports[i].Width=(float)((double)viewports[i].Width*enhanced_width/native_width);
        viewports[i].Height=(float)((double)viewports[i].Height*enhanced_height/native_height);
    }
    for(UINT i=0;i<draw->scissor_count;++i) {
        if(!pilot_native_draw_edge(scissors[i].left,enhanced_width,native_width,0,&scissors[i].left) ||
           !pilot_native_draw_edge(scissors[i].right,enhanced_width,native_width,1,&scissors[i].right) ||
           !pilot_native_draw_edge(scissors[i].top,enhanced_height,native_height,0,&scissors[i].top) ||
           !pilot_native_draw_edge(scissors[i].bottom,enhanced_height,native_height,1,&scissors[i].bottom)) {
            pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_SCISSOR,(int)i,NULL,0,S_FALSE);goto skipped;
        }
    }
    if(!immediate) {
        if(color_resource && color_owned)ID3D11Resource_Release(color_resource);
        if(depth_resource && depth_owned)ID3D11Resource_Release(depth_resource);
        draw->enhanced=1;return S_OK;
    }
    /* All admission is complete before beginning the optional child query.
     * There are no failing/skip paths between Begin and the matching end(). */
    if(!pilot_visibility_internal_try_begin(&draw->excluded_query)) {
        pilot_native_draw_diagnostic(draw,PILOT_NATIVE_REASON_QUERY,-1,NULL,0,S_FALSE);goto skipped;
    }
    if(color_resource && color_owned)ID3D11Resource_Release(color_resource);
    if(depth_resource && depth_owned)ID3D11Resource_Release(depth_resource);
    /* Native SRVs cannot reference private enhanced outputs. Admitted HD
     * inputs were also checked against both enhanced attachments above.
     * Changing OM therefore cannot clear a live PS binding. Avoid the old
     * NULL/rebind cycle, and leave ordinary resident textures bound when no
     * render-target input actually needs remapping. */
    if(!draw->binding_reuse) {
        ID3D11ShaderResourceView *none[PILOT_NATIVE_DRAW_SRVS]={0};
        ID3D11DeviceContext_PSSetShaderResources(context,0,PILOT_NATIVE_DRAW_SRVS,none);
    }
    ID3D11DeviceContext_OMSetRenderTargets(context,draw->enhanced_color?1:0,&draw->enhanced_color,draw->enhanced_depth);
    if(!draw->binding_reuse || draw->input_remap)
        ID3D11DeviceContext_PSSetShaderResources(context,0,PILOT_NATIVE_DRAW_SRVS,draw->enhanced_inputs);
    ID3D11DeviceContext_RSSetViewports(context,draw->viewport_count,viewports);
    ID3D11DeviceContext_RSSetScissorRects(context,draw->scissor_count,scissors);
    draw->bound=draw->enhanced=1;return S_OK;
skipped:
    result=S_FALSE;
invalid:
    pilot_native_draw_invalidate(draw);
finished:
    if(color_resource && color_owned)ID3D11Resource_Release(color_resource);
    if(depth_resource && depth_owned)ID3D11Resource_Release(depth_resource);
    pilot_native_draw_release(draw);return result;
}

static int pilot_native_draw_end(struct PilotNativeDrawState *draw)
{
    int enhanced=(int)draw->enhanced;
    pilot_visibility_internal_end(draw->excluded_query);draw->excluded_query=NULL;
    if(draw->bound) {
        /* HD inputs are private companions or unchanged native inputs; none
         * can alias the original attachments being restored. */
        if(!draw->binding_reuse) {
            ID3D11ShaderResourceView *none[PILOT_NATIVE_DRAW_SRVS]={0};
            ID3D11DeviceContext_PSSetShaderResources(draw->context,0,PILOT_NATIVE_DRAW_SRVS,none);
        }
        ID3D11DeviceContext_OMSetRenderTargets(draw->context,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,draw->outputs,draw->depth);
        if(!draw->binding_reuse || draw->input_remap)
            ID3D11DeviceContext_PSSetShaderResources(draw->context,0,PILOT_NATIVE_DRAW_SRVS,draw->inputs);
        ID3D11DeviceContext_RSSetViewports(draw->context,draw->viewport_count,draw->viewports);
        ID3D11DeviceContext_RSSetScissorRects(draw->context,draw->scissor_count,draw->scissors);
    }
    pilot_native_draw_release(draw);return enhanced;
}
static HRESULT pilot_native_draw_begin(struct PilotNativeDrawState *draw)
{return pilot_native_draw_prepare(draw,1);}
#include "graphics_native_plane_epoch.inc"
static void pilot_native_draw_enhanced(UINT count,UINT first,INT base,int indexed)
{
    if(!pilot_native_enabled()) {memset(pilot_native_epoch_extents,0,sizeof(pilot_native_epoch_extents));return;}
    uint64_t cost_work=pilot_cost_companion_begin();
    if(!pilot_native_epoch_enabled()) {
        /* Matched opt-out retains the original single-admission immediate
         * path; it must not pay for a failed deferred capture first. */
        pilot_native_epoch_flush_reason(COST_EPOCH_ADMISSION);
        struct PilotNativeDrawState direct;
        if(pilot_native_draw_begin(&direct)==S_OK) {
            pilot_cost_add(COST_COMPANION_DRAWS,1);
            if(indexed)ID3D11DeviceContext_DrawIndexed(direct.context,count,first,base);
            else ID3D11DeviceContext_Draw(direct.context,count,first);
            (void)pilot_native_draw_end(&direct);
        }
        goto finished;
    }
    struct PilotNativeDrawState draw;HRESULT result=pilot_native_draw_prepare(&draw,0);
    if(result==S_OK) {
        int recorded=pilot_native_epoch_record(&draw,count,first,base,indexed);
        if(recorded) {
            (void)pilot_native_draw_end(&draw);goto finished;
        }
        (void)pilot_native_draw_end(&draw);
    }
    /* Earlier HD commands must precede either current immediate fallback or
     * a declined current replay. Re-admit after flushing in case it degraded
     * a queued companion following an optional query/device failure. */
    pilot_native_epoch_flush_reason(COST_EPOCH_ADMISSION);
    if(result==S_OK && pilot_native_draw_begin(&draw)==S_OK) {
        pilot_cost_add(COST_COMPANION_DRAWS,1);
        if(indexed)ID3D11DeviceContext_DrawIndexed(draw.context,count,first,base);
        else ID3D11DeviceContext_Draw(draw.context,count,first);
        (void)pilot_native_draw_end(&draw);
    }
finished:
    pilot_cost_companion_end(cost_work);
    memset(pilot_native_epoch_extents,0,sizeof(pilot_native_epoch_extents));
}
static void pilot_native_draw(UINT count,UINT first)
{
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_Draw(context,count,first);
    pilot_native_draw_enhanced(count,first,0,0);
    /* Optional HD failure never changes the already submitted native draw. */
}
static void pilot_native_draw_indexed(UINT count,UINT first,INT base)
{
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_DrawIndexed(context,count,first,base);
    pilot_native_draw_enhanced(count,first,base,1);
}
#else
/* Other supported donors retain the original single native GPU submission. */
static void pilot_native_epoch_flush_reason(unsigned reason){(void)reason;}
#define pilot_native_epoch_flush() pilot_native_epoch_flush_reason(COST_EPOCH_OTHER)
static void pilot_native_epoch_close(void){}
static void pilot_native_epoch_vertex_span(unsigned stream,unsigned extent){(void)stream;(void)extent;}
struct PilotNativeDrawState {unsigned unused;};
static HRESULT pilot_native_draw_begin(struct PilotNativeDrawState *draw)
{memset(draw,0,sizeof(*draw));return S_FALSE;}
static int pilot_native_draw_end(struct PilotNativeDrawState *draw)
{(void)draw;return 0;}
static void pilot_native_draw_invalidate(struct PilotNativeDrawState *draw)
{(void)draw;}
static void pilot_native_draw(UINT count,UINT first)
{ID3D11DeviceContext_Draw(d3d8_GetD3D11Context(),count,first);}
static void pilot_native_draw_indexed(UINT count,UINT first,INT base)
{ID3D11DeviceContext_DrawIndexed(d3d8_GetD3D11Context(),count,first,base);}
#endif
