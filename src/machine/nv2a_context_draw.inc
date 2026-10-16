// Loaded-context consumer admission. This selects dependencies, not defaults or
// a replacement renderer. The existing generic software path still executes.
bool Nv2a::context_draw_ready(std::array<uint8_t,13>& observable) {
    auto reject=[&](const char* why){fault_(why);return false;};
    auto known=[&](size_t word,uint32_t mask=0xffffffffu) {
        if((context_raster_known_[word]&mask)==mask)return true;
        char why[128];
        std::snprintf(why,sizeof(why),"NV2A loaded draw raster word %zu needs mask %08x",word,mask);
        return reject(why);
    };
    if(renderer_)return reject("NV2A loaded draw requires the software reference");
    if((context_known_&(kContextTransformMode|kContextProgramStart))!=
       (kContextTransformMode|kContextProgramStart))
        return reject("NV2A loaded draw transform selector is unknown");
    if(transform_execution_mode_!=2 || primitive_mode_<5 || primitive_mode_>8)
        return reject("NV2A loaded draw requires programmable filled triangles/quads");
    if(!context_initializer_draw_ready())return false;
    for(size_t word:{kRasterSurfaceFormat,kRasterSurfaceColorOffset,
                    kRasterSurfaceClipHorizontal,kRasterSurfaceClipVertical,
                    kRasterWindowClipHorizontal,kRasterWindowClipVertical,
                    kRasterControl0,kRasterShadeMode,kRasterFrontPolygonMode,
                    kRasterBackPolygonMode,kRasterZMinMaxControl,kRasterColorMask,
                    kRasterDepthMask,kRasterClipMin,kRasterClipMax,
                    kRasterCombinerControl,kRasterCombinerSpecularFog0,
                    kRasterCombinerSpecularFog1,kRasterShaderStageProgram})
        if(!known(word))return false;
    // Only fourteen setters exist in this internal enable word; upper bits are
    // not hardware state. Every actual lower-bit feature selection is explicit.
    if(!known(kRasterEnables,0x3fff) || !known(kRasterSurfacePitch,0xffff))return false;
    if(!color_dma_bound_ || !pramin_range_initialized(color_dma_,12))
        return reject("NV2A loaded draw color DMA descriptor is unknown");
    const uint32_t enables=raster_state_[kRasterEnables];
    const bool depth_test=(enables&8)!=0, stencil_test=(enables&32)!=0;
    if(depth_test && !known(kRasterDepthFunc))return false;
    if(stencil_test) {
        for(size_t word:{kRasterStencilFunc,kRasterStencilRef,kRasterStencilFuncMask,
                         kRasterStencilWriteMask,kRasterStencilOpFail,
                         kRasterStencilOpZFail,kRasterStencilOpZPass})
            if(!known(word))return false;
    }
    const bool wants_zeta=(depth_test && raster_state_[kRasterDepthFunc]!=0x207) ||
        stencil_test || raster_state_[kRasterDepthMask]!=0;
    if(wants_zeta) {
        if(!known(kRasterSurfaceZetaOffset) || !known(kRasterSurfacePitch,0xffff0000))return false;
        if(!zeta_dma_bound_ || !pramin_range_initialized(zeta_dma_,12))
            return reject("NV2A loaded draw zeta DMA descriptor is unknown");
        // The ordinary surface resolver checks layout/extent/aliasing; tiled
        // reads require initialized tags. Loaded compressed ZROP writes remain
        // guarded there until their writer owns the compression selector.
    }
    if(raster_state_[kRasterShadeMode]==0x1d00 && !known(kRasterProvokingVertex))return false;
    if((enables&4) && (!known(kRasterCullFace) || !known(kRasterFrontFace)))return false;
    if((enables&1) && (!known(kRasterAlphaFunc) || !known(kRasterAlphaRef,0xff)))return false;
    if((enables&0x40) && !known(kRasterLogicOp))return false;
    const bool logic_copy=(enables&0x40) && raster_state_[kRasterLogicOp]==0x1503;
    if((enables&2) && !logic_copy && (!known(kRasterBlendSrc) ||
       !known(kRasterBlendDst) || !known(kRasterBlendEquation)))return false;
    if((enables&0x2000) && (!known(kRasterPolygonOffsetScale) ||
       !known(kRasterPolygonOffsetBias)))return false;

    observable.fill(0);
    observable[0]=15;
    // The generic floor interpolates both colors for its general evaluator.
    // Requiring them also covers its simpler passthrough/modulate cases.
    observable[3]=observable[4]=15;
    std::array<bool,4> textures{};
    bool fog=false;
    auto input=[&](uint32_t byte,size_t factor0,size_t factor1) {
        const unsigned reg=byte&15;
        if(reg==1 && !known(factor0))return false;
        if(reg==2 && !known(factor1))return false;
        fog|=reg==3;
        if(reg>=8 && reg<=11)textures[reg-8]=true;
        return true;
    };
    const unsigned stages=std::min(raster_state_[kRasterCombinerControl]&255u,8u);
    for(unsigned stage=0;stage<stages;++stage) {
        for(size_t base:{kRasterCombinerColorIcw0,kRasterCombinerAlphaIcw0,
                         kRasterCombinerColorOcw0,kRasterCombinerAlphaOcw0})
            if(!known(base+stage))return false;
        for(size_t base:{kRasterCombinerColorIcw0,kRasterCombinerAlphaIcw0})
            for(unsigned shift=0;shift<32;shift+=8)
                if(!input(raster_state_[base+stage]>>shift,
                          kRasterCombinerFactor0_0+stage,kRasterCombinerFactor1_0+stage))return false;
    }
    for(unsigned shift=0;shift<32;shift+=8)
        if(!input(raster_state_[kRasterCombinerSpecularFog0]>>shift,
                  kRasterCombinerFinalFactor0,kRasterCombinerFinalFactor1))return false;
    for(unsigned shift=8;shift<32;shift+=8)
        if(!input(raster_state_[kRasterCombinerSpecularFog1]>>shift,
                  kRasterCombinerFinalFactor0,kRasterCombinerFinalFactor1))return false;
    if(fog) {
        if(!known(kRasterFogColor) || !known(kRasterFogEnable))return false;
        if(raster_state_[kRasterFogEnable]) {
            if(!known(kRasterFogMode))return false;
            for(unsigned i=0;i<3;++i)if(!known(kRasterFogParams0+i))return false;
            observable[5]=1;
        }
    }
    for(unsigned stage=0;stage<4;++stage) {
        const unsigned mode=(raster_state_[kRasterShaderStageProgram]>>(stage*5))&31;
        if(mode!=0 && mode!=1 && mode!=3)
            return reject("NV2A loaded draw texture shader side effects are unqualified");
        if(!textures[stage])continue;
        for(size_t base:{kRasterTextureOffset0,kRasterTextureFormat0,kRasterTextureAddress0,
                         kRasterTextureFilter0,kRasterTextureControl0_0})
            if(!known(base+stage))return false;
        const uint32_t format=raster_state_[kRasterTextureFormat0+stage];
        const unsigned color=(format>>8)&255;
        const bool normalized=color==4 || color==6 || color==7 || color==0x19 ||
            ((color==0xc || color==0xe || color==0xf) && ((format>>16)&15));
        if(mode==1 && !normalized &&
           (!known(kRasterTextureControl1_0+stage) || !known(kRasterTextureImageRect0+stage)))return false;
        const uint32_t address=raster_state_[kRasterTextureAddress0+stage];
        if(((address&255)==4 || ((address>>8)&255)==4) &&
           !known(kRasterTextureBorderColor0+stage))return false;
        if((color==0x24 || color==0x25) && !known(kRasterControl0,1u<<28))return false;
        if(ram_tiles_active() && !known(kRasterTexturePalette0+stage))return false;
        // The real fetch helper validates the selected DMA and full extent.
        // No array/current-value knowledge is manufactured by this selection.
        observable[9+stage]=mode==3 ? 7 : mode==1 ? 11 : 3;
    }
    return true;
}

bool Nv2a::context_vertex_inputs_ready(const std::array<uint8_t,16>& required) {
    for(unsigned slot=0;slot<16;++slot) {
        if((context_vertex_current_known_[slot]&required[slot])==required[slot])continue;
        char why[128];
        std::snprintf(why,sizeof(why),"NV2A loaded vertex input %u needs components %x (known %x)",
                      slot,required[slot],context_vertex_current_known_[slot]);
        fault_(why);return false;
    }
    return true;
}
