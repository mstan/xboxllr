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
    if(renderer_ && !gpu_phase_depth_)
        return reject("NV2A loaded draw renderer has no coherent device phase");
    if((context_known_&(kContextTransformMode|kContextProgramStart))!=
       (kContextTransformMode|kContextProgramStart))
        return reject("NV2A loaded draw transform selector is unknown");
    const bool primitive_ready=primitive_mode_>=5 && primitive_mode_<=8;
    if(transform_execution_mode_!=2 || !primitive_ready) {
        char reason[192];std::snprintf(reason,sizeof(reason),
            "NV2A loaded draw requires supported programmable primitive: transform=%u primitive=%u line-width=%u/%08x point-size=%u/%08x",
            unsigned(transform_execution_mode_),unsigned(primitive_mode_),unsigned(context_initializers_.line_width.known),context_initializers_.line_width.value,
            unsigned(context_initializers_.point_size.known),context_initializers_.point_size.value);
        return reject(reason);
    }
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
        // reads require initialized tags. Compressed writes additionally need
        // the actual1D80 selector before any target effects.
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
#if !XLLE_RENDERER_HLE
    observable[3]=observable[4]=15;
#endif
    std::array<bool,4> textures{};
    bool fog=false;
    auto input=[&](uint32_t byte,size_t factor0,size_t factor1,bool alpha=false) {
        const unsigned reg=byte&15;
#if XLLE_RENDERER_HLE
        // The host shader consumes colors only when selected by the pixel
        // combiner, unlike the generic floor's unconditional interpolation.
        const uint8_t lanes=(byte&0x10) ? 8 : alpha ? 4 : 7;
        if(reg==4)observable[3]|=lanes;
        if(reg==5)observable[4]|=lanes;
        // V1+R0 has RGB only; its alpha is literal zero in the evaluator.
        if(reg==14)observable[4]|=uint8_t(lanes&7);
#endif
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
                          kRasterCombinerFactor0_0+stage,kRasterCombinerFactor1_0+stage,base==kRasterCombinerAlphaIcw0))return false;
    }
    for(unsigned shift=0;shift<32;shift+=8)
        if(!input(raster_state_[kRasterCombinerSpecularFog0]>>shift,
                  kRasterCombinerFinalFactor0,kRasterCombinerFinalFactor1))return false;
    for(unsigned shift=8;shift<32;shift+=8)
        if(!input(raster_state_[kRasterCombinerSpecularFog1]>>shift,
                  kRasterCombinerFinalFactor0,kRasterCombinerFinalFactor1,shift==8))return false;
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
#if XLLE_RENDERER_HLE
        const bool supported_mode=mode<=4; // NONE, PROJECT2D/3D, CUBE, PASSTHRU
#else
        const bool supported_mode=mode==0 || mode==1 || mode==3;
#endif
        if(!supported_mode) {
            char reason[256];std::snprintf(reason,sizeof(reason),
                "NV2A loaded draw texture shader side effects are unqualified: stage=%u mode=%u program=%08x other=%08x control=%08x format=%08x",
                stage,mode,raster_state_[kRasterShaderStageProgram],raster_state_[kRasterShaderOtherStageInput],
                raster_state_[kRasterTextureControl0_0+stage],raster_state_[kRasterTextureFormat0+stage]);
            return reject(reason);
        }
#if XLLE_RENDERER_HLE
        if(textures[stage])observable[9+stage]=mode==0?0:mode==1?11:mode==3?7:15;
        // HLE snapshots all sampling stages before submission, including a
        // sampler whose result is subsequently dead in the pixel combiner.
        if(mode==0 || mode==4)continue;
#else
        if(!textures[stage])continue;
#endif
        for(size_t base:{kRasterTextureOffset0,kRasterTextureFormat0,kRasterTextureAddress0,
                         kRasterTextureFilter0,kRasterTextureControl0_0})
            if(!known(base+stage))return false;
        const uint32_t format=raster_state_[kRasterTextureFormat0+stage];
        const unsigned color=(format>>8)&255;
        if(color==0xb)
            return reject("NV2A loaded indexed texture sampling is unqualified");
        const bool normalized=color==4 || color==6 || color==7 || color==0x19 ||
            ((color==0xc || color==0xe || color==0xf) && ((format>>16)&15));
#if XLLE_RENDERER_HLE
        if((mode==1 || mode==2) && !normalized &&
#else
        if(mode==1 && !normalized &&
#endif
           (!known(kRasterTextureControl1_0+stage) || !known(kRasterTextureImageRect0+stage)))return false;
#if XLLE_RENDERER_HLE
        if(mode==2 && (color==0x30 || color==0x2c || color==0x2e) &&
           !known(kRasterShadowDepthFunc,7))return false;
#endif
        const uint32_t address=raster_state_[kRasterTextureAddress0+stage];
        if(((address&255)==4 || ((address>>8)&255)==4) &&
           !known(kRasterTextureBorderColor0+stage))return false;
        if((color==0x24 || color==0x25) && !known(kRasterControl0,1u<<28))return false;
        // Supported generic textures carry direct color/alpha, compressed
        // blocks or packed YUV. None consumes a palette, with or without RAM
        // tiles. Indexed sampling remains guarded above; no unknown palette
        // state is initialized or made known by this admission.
        // The real fetch helper validates the selected DMA and full extent.
        // No array/current-value knowledge is manufactured by this selection.
#if !XLLE_RENDERER_HLE
        observable[9+stage]=mode==3 ? 7 : mode==1 ? 11 : 3;
#endif
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

// Reuse the existing loaded-context dependency contracts before HLE raw-vertex
// submission. This decodes attributes/address dependencies, never executes a
// vertex shader or produces CPU-transformed vertices for the host renderer.
bool Nv2a::context_hle_draw_inputs_ready() {
    if(!pgraph_loaded_context_)return true;
    std::array<uint8_t,13> observable{};
    if(!context_draw_ready(observable))return false;
    const bool indexed=!vertex_indices_.empty(),inlined=!inline_vertices_.empty();
    const size_t count=indexed?vertex_indices_.size():inlined?inline_vertices_.size():immediate_vertices_.size();
    if(indexed && context_vertex_format_known_!=0xffff) {
        fault_("NV2A loaded indexed array formats are unknown");return false;
    }
    if((inlined && context_inline_known_.size()!=count) ||
       (!indexed && !inlined && context_immediate_known_.size()!=count)) {
        fault_("NV2A loaded vertex snapshot provenance is missing");return false;
    }
    bool final=false;
    for(size_t slot=transform_program_start_;slot<transform_program_.size();++slot) {
        if(context_program_known_[slot]!=15) {
            fault_("NV2A loaded shader executed program words are unknown");return false;
        }
        if(transform_program_[slot][3]&1u){final=true;break;}
    }
    if(!final){fault_("NV2A loaded shader has no known FINAL");return false;}
    Nv2aVshProgram program{};
    const size_t slots=transform_program_.size()-transform_program_start_;
    if(nv2a_vsh_parse_program(&program,transform_program_[transform_program_start_].data(),uint32_t(slots))!=NV2AVPR_SUCCESS) {
        fault_("NV2A loaded shader dependency parse failed");return false;
    }
    struct Release {Nv2aVshProgram& p;~Release(){nv2a_vsh_program_destroy(&p);}} release{program};
    const bool per_vertex=vsh_dependencies_need_vertex(program.steps,slots);
    VshDependencies deps;const char* error=nullptr;
    if(!per_vertex && !vsh_dependencies(program.steps,slots,observable,deps,error)) {
        fault_(error?error:"NV2A loaded shader dependency analysis failed");return false;
    }
    for(size_t vertex=0;vertex<count;++vertex) {
        if(indexed) {
            for(unsigned slot=0;slot<16;++slot)
                if(vertex_attributes_[slot].count && !fetch_vertex_attribute(slot,vertex_indices_[vertex]))return false;
        } else {
            const auto& inputs=inlined?inline_vertices_[vertex]:immediate_vertices_[vertex];
            for(unsigned slot=0;slot<16;++slot)vertex_attributes_[slot].value=inputs[slot];
            context_vertex_current_known_=inlined?context_inline_known_[vertex]:context_immediate_known_[vertex];
        }
        if(per_vertex) {
            std::array<std::array<float,4>,16> inputs;
            for(unsigned slot=0;slot<16;++slot)inputs[slot]=vertex_attributes_[slot].value;
            if(!vsh_dependencies_for_vertex(program.steps,slots,observable,inputs,
                    context_vertex_current_known_,transform_constants_,context_constants_known_,deps,error)) {
                fault_(error?error:"NV2A loaded vertex dependency analysis failed");return false;
            }
        }
        for(size_t constant=0;constant<deps.constants.size();++constant)
            if((context_constants_known_[constant]&deps.constants[constant])!=deps.constants[constant]) {
                fault_("NV2A loaded shader constant components are unknown");return false;
            }
        auto required_inputs=deps.inputs;
#if XLLE_RENDERER_HLE
        for(unsigned slot=0;slot<16;++slot) {
            const uint8_t missing=required_inputs[slot]&uint8_t(~context_vertex_current_known_[slot]);
            uint32_t bits;std::memcpy(&bits,&vertex_attributes_[slot].value[3],sizeof(bits));
            if(missing==8 && !vertex_attributes_[slot].count &&
               (hle_cold_default_w_&(1u<<slot)) && bits==0x3f800000u) {
                required_inputs[slot]&=uint8_t(~8u);++hle_default_w_uses_;
                if(!(hle_default_w_reported_&(1u<<slot))) {
                    std::fprintf(stderr,"[nv2a] HLE approximation: fresh first-context untouched disabled input%u W=1 API default (VAB LOAD projection deferred)\n",slot);
                    hle_default_w_reported_|=uint16_t(1u<<slot);
                }
            }
        }
#endif
        bool missing_input=false;
        for(unsigned slot=0;slot<16;++slot)
            missing_input|=(context_vertex_current_known_[slot]&required_inputs[slot])!=required_inputs[slot];
        if(missing_input && !vertex_batches_) {
            std::fprintf(stderr,"[nv2a] first HLE input fault indexed=%u inlined=%u vertices=%zu vertex=%zu format-known=%04x program-start=%u\n",
                unsigned(indexed),unsigned(inlined),count,vertex,unsigned(context_vertex_format_known_),unsigned(transform_program_start_));
            std::fprintf(stderr,"[nv2a] pixel control=%08x final=%08x/%08x enables=%08x mask=%08x alpha=%08x/%08x blend=%08x/%08x/%08x\n",
                raster_state_[kRasterCombinerControl],raster_state_[kRasterCombinerSpecularFog0],raster_state_[kRasterCombinerSpecularFog1],
                raster_state_[kRasterEnables],raster_state_[kRasterColorMask],raster_state_[kRasterAlphaFunc],raster_state_[kRasterAlphaRef],
                raster_state_[kRasterBlendSrc],raster_state_[kRasterBlendDst],raster_state_[kRasterBlendEquation]);
            for(unsigned stage=0;stage<std::min(raster_state_[kRasterCombinerControl]&255u,8u);++stage)
                std::fprintf(stderr,"[nv2a] pixel stage%u rgb=%08x/%08x alpha=%08x/%08x\n",stage,
                    raster_state_[kRasterCombinerColorIcw0+stage],raster_state_[kRasterCombinerColorOcw0+stage],
                    raster_state_[kRasterCombinerAlphaIcw0+stage],raster_state_[kRasterCombinerAlphaOcw0+stage]);
            for(unsigned slot=0;slot<16;++slot) {
                const auto& attr=vertex_attributes_[slot];std::array<uint32_t,4> bits;
                std::memcpy(bits.data(),attr.value.data(),sizeof(bits));
                std::fprintf(stderr,"[nv2a] input%u format=%u count=%u size=%u stride=%u offset=%08x dmaB=%u need=%x known=%x value=%08x,%08x,%08x,%08x\n",
                    slot,unsigned(attr.format),unsigned(attr.count),unsigned(attr.element_size),unsigned(attr.stride),attr.offset,unsigned(attr.dma_b),
                    unsigned(deps.inputs[slot]),unsigned(context_vertex_current_known_[slot]),bits[0],bits[1],bits[2],bits[3]);
            }
            for(size_t constant=0;constant<deps.constants.size();++constant)
                if(deps.constants[constant])std::fprintf(stderr,"[nv2a] constant%zu need=%x known=%x\n",constant,
                    unsigned(deps.constants[constant]),unsigned(context_constants_known_[constant]));
            for(size_t slot=transform_program_start_;slot<transform_program_.size();++slot) {
                const auto& op=transform_program_[slot];
                std::fprintf(stderr,"[nv2a] program%zu known=%x words=%08x,%08x,%08x,%08x\n",slot,
                    unsigned(context_program_known_[slot]),op[0],op[1],op[2],op[3]);
                if(op[3]&1u)break;
            }
        }
        if(!context_vertex_inputs_ready(required_inputs))return false;
    }
    return true;
}
