// Loaded-context provenance. Included after the ordinary NV097 handler table.
// The fixture/HLE fast path keeps its existing dispatch; only an actual LOAD
// uses this admission and knowledge. Unknown state is retained, never zeroed.
bool Nv2a::pramin_range_initialized(uint32_t offset,size_t size) const {
    if(uint64_t(offset)+size>pramin_.size())return false;
    for(size_t i=offset;i<uint64_t(offset)+size;++i)
        if(!(pramin_initialized_[i/64]&(uint64_t(1)<<(i%64))))return false;
    return true;
}

bool Nv2a::context_method_ready(const PushMethod& push) {
    auto reject=[&](const char* reason) {
        char why[224];
        std::snprintf(why,sizeof(why),"NV2A loaded context ch%u/sub%u method %04x parameter %08x: %s",
                      push.channel,push.subchannel,push.method,push.parameter,reason);
        fault_(why);return false;
    };
    if(pgraph_method_faulted_ || draw_faulted_)return reject("previous method failed");
    const uint32_t m=push.method, p=push.parameter;
    const auto sub=push.subchannel;
    const auto cls=object_class_[sub];
    auto known=[&](uint64_t mask){return (context_known_&mask)==mask;};
    auto raster=[&](size_t word,uint32_t mask=0xffffffffu) {
        return (context_raster_known_[word]&mask)==mask;
    };
    if(m==0 || m==0x50)return true;
    if(!(context_objects_known_&(1u<<sub)))return reject("object binding is unknown");
    const auto initializer=context_initializer_method(push,false);
    if(initializer!=ContextInitializerResult::Unhandled)
        return initializer==ContextInitializerResult::Ready;
    if(cls==0x62) {
        switch(m) {
        case 0x184:case 0x188:case 0x300:return true;
        case 0x304:
            if(p&~0xffc0ffc0u)return reject("surface pitch encoding is unsupported");
            return true;
        case 0x308:case 0x30c:
            if(p&~0x07ffffc0u)return reject("surface offset encoding is unsupported");
            return true;
        default:return reject("2D method semantics are unqualified");
        }
    }
    if(cls==0x9f) {
        switch(m) {
        case 0x19c:case 0x2fc:case 0x300:case 0x304:return true;
        case 0x308:
            {
                // The copy path implements disabled chroma/clip/pattern/ROP/
                // beta selectors. A LOAD or another object's receipts cannot
                // establish those selections for the current blit owner.
                bool selectors=false;
                for(const auto& owner:context_initializers_.objects) {
                    if(!owner.used || owner.channel!=push.channel || owner.object_class!=cls ||
                       owner.instance!=object_instance_[sub])continue;
                    uint32_t identity=0;
                    if(!pramin_range_initialized(owner.instance,4))continue;
                    std::memcpy(&identity,pramin_.data()+owner.instance,4);
                    if(identity!=owner.identity_word)continue;
                    selectors=std::all_of(owner.null_selectors.begin(),owner.null_selectors.end(),
                        [](const auto& binding){return binding.known && (binding.words[0]&0xfff)==0x30;});
                    if(selectors)break;
                }
                if(!selectors)return reject("2D copy context selectors are unknown");
            }
            if(!known(kContext2dObject|kContext2dFormat|kContextBlitObject|
                      kContextBlitOperation|kContextBlitIn|kContextBlitOut) ||
               !image_blit_.context_surfaces_bound || !context_surfaces_2d_.dma_source_bound ||
               !context_surfaces_2d_.dma_dest_bound)return reject("2D copy input is unknown");
            if(image_blit_.operation!=3)return reject("2D operation is unqualified");
            return true;
        default:return reject("blit method semantics are unqualified");
        }
    }
    if(cls==0x44 && m==0x310)return true;
    if(cls!=0x97 || m>=kNv097MethodLimit || (m&3) ||
       kNv097Table.handler[m>>2]==kNv097None)return reject("method semantics are unqualified");
    switch(kNv097Table.handler[m>>2]) {
    case kNv097M012C:
        if(!known(kContextFlipWrite|kContextFlipModulo))return reject("flip producer state is unknown");
        break;
    case kNv097M0130:
        if(!known(kContextFlipRead|kContextFlipWrite|kContextFlipModulo))return reject("flip queue state is unknown");
        break;
    case kNv097M020C:
        if(p&~0xffc0ffc0u)return reject("surface pitch encoding is unsupported");
        break;
    case kNv097M0210:case kNv097M0214:
        if(p&~0x07ffffc0u)return reject("surface offset encoding is unsupported");
        break;
    case kNv097M1D94:
        // Check BOTH selected planes before the first write; unused clear
        // values, bindings and ordinary raster features are not dependencies.
        if(!p)break;
        if(!context_initializer_clear_ready(p))return false;
        if(!known(kContextClearH|kContextClearV) || !raster(kRasterSurfaceFormat,0xff00))
            return reject("clear rectangle/format is unknown");
        if((p&0xf0) && (!known(kContextClearColor) || !color_dma_bound_ ||
             !pramin_range_initialized(color_dma_,12) ||
             !raster(kRasterSurfaceFormat,0xf) || !raster(kRasterSurfacePitch,0xffff) ||
             !raster(kRasterSurfaceColorOffset)))return reject("color clear input is unknown");
        if((p&3) && (!known(kContextClearZeta) || !zeta_dma_bound_ ||
             !pramin_range_initialized(zeta_dma_,12) ||
             !raster(kRasterSurfaceFormat,0xf0) || !raster(kRasterSurfacePitch,0xffff0000) ||
             !raster(kRasterSurfaceZetaOffset)))return reject("depth clear input is unknown");
        if(!known(kContextCompression)) {
            // Follow the same first-byte tile selection as clear_surface_plane.
            // A linear clear has no compression-selector dependency. Inspect
            // both selected planes before permitting either one to write.
            for(unsigned plane=0;plane<2;++plane) {
                if(!(p&(plane?3u:0xf0u)))continue;
                const unsigned format=(surface_format_>>(plane?4:0))&15;
                const unsigned bpp=plane ? (format==1?2:format==2?4:0) :
                    (format>=1 && format<=3?2:format>=4 && format<=8?4:0);
                if(!bpp)return reject("clear format is unsupported");
                const uint32_t pitch=plane?surface_pitch_>>16:surface_pitch_&0xffff;
                const uint64_t offset=uint64_t(plane?surface_zeta_offset_:surface_color_offset_)+
                    uint64_t(clear_rect_vertical_&0xffff)*pitch+
                    uint64_t(clear_rect_horizontal_&0xffff)*bpp;
                uint32_t address=0;
                if(offset>UINT32_MAX || !dma_object_address(plane?zeta_dma_:color_dma_,uint32_t(offset),address))
                    return reject("clear DMA extent is invalid");
                const int tile=ram_tile_at(address);
                if(tile>=0 && (pgraph_tiles_.zcomp[unsigned(tile)]&0x80000000u))
                    return reject("clear compression selector is unknown");
            }
        }
        break;
    case kNv097R0B00_0B80:
        if(!known(kContextProgramLoad))return reject("program upload cursor is unknown");
        break;
    case kNv097R0B80_0C00:
        if(!known(kContextConstantLoad))return reject("constant upload cursor is unknown");
        break;
    case kNv097R1B00_1C00: {
        const unsigned local=(m-0x1b00)&0x3f;
        if(local>0x24 || local==0x18)return reject("texture method semantics are unqualified");
        break;
    }
    case kNv097M1D70: {
        if(!(context_semaphore_dma_known_&(1u<<sub)) ||
           !(context_semaphore_offset_known_&(1u<<sub)))return reject("semaphore input is unknown");
        uint32_t first=0,last=0;
        const uint32_t offset=semaphore_offset_[sub];
        if((offset&3) || offset>0xfffffffcu ||
           !dma_object_address(semaphore_dma_[sub],offset,first) ||
           !dma_object_address(semaphore_dma_[sub],offset+3,last) || last!=uint64_t(first)+3)
            return reject("semaphore DMA extent is invalid");
        break;
    }
    case kNv097M17CC:
        if(p>1)return reject("visibility enable must be boolean");
        break;
    case kNv097M17F8:
        if(p&~0xffffu)return reject("shader clip-plane comparison encoding is unsupported");
        break;
    case kNv097M17D0:
        if(!known(kContextZpassCount) || !report_dma_bound_[sub])return reject("report input is unknown");
        break;
    case kNv097M17FC:
        if(p && (p<5 || p>8 || primitive_mode_))
            return reject("BEGIN primitive/nesting is unqualified");
        // END checks the actual latched producers in submit_vertex_batch.
        break;
    case kNv097M1800:case kNv097M1808:case kNv097M1810:case kNv097M1818:
        if(!primitive_mode_)return reject("vertex data outside BEGIN/END");
        break;
    default:break;
    }
    return true;
}

void Nv2a::context_method_completed(const PushMethod& push) {
    const auto m=push.method;
    const auto sub=push.subchannel;
    const auto cls=object_class_[sub];
    auto& image=*pgraph_loaded_context_;
    auto dma_changed=[&](unsigned slot) {
        // The explicit binding identifies the descriptor used by memory
        // consumers. Its raw register/cache projection is a separate contract.
        image.invalidate_surface_word(6+slot);image.invalidate_surface_word(17+slot);
    };
    if(m==0) {
        context_objects_known_|=uint8_t(1u<<sub);
        if(cls==0x62)context_known_|=kContext2dObject;
        if(cls==0x9f)context_known_|=kContextBlitObject;
        return;
    }
    if(cls==0x62) {
        switch(m) {
        case 0x184:dma_changed(1);break;
        case 0x188:dma_changed(0);break;
        case 0x300:context_known_|=kContext2dFormat;break;
        case 0x304:image.set_surface_word(12,context_surfaces_2d_.dest_pitch);
                   image.set_surface_word(13,context_surfaces_2d_.source_pitch);break;
        case 0x308:image.set_surface_word(1,context_surfaces_2d_.source_offset);break;
        case 0x30c:image.set_surface_word(0,context_surfaces_2d_.dest_offset);break;
        default:break;
        }
        return;
    }
    if(cls==0x9f) {
        if(m==0x2fc)context_known_|=kContextBlitOperation;
        if(m==0x300)context_known_|=kContextBlitIn;
        if(m==0x304)context_known_|=kContextBlitOut;
        return;
    }
    if(cls==0x44 && m==0x310)context_known_|=kContextPatternColor;
    if(cls!=0x97)return;
    switch(m) {
    case 0x120:context_known_|=kContextFlipRead;break;
    case 0x124:context_known_|=kContextFlipWrite;break;
    case 0x128:context_known_|=kContextFlipModulo;break;
    case 0x194:dma_changed(2);break;
    case 0x198:dma_changed(3);break;
    case 0x20c:image.set_surface_word(14,surface_pitch_&0xffff);
               image.set_surface_word(15,surface_pitch_>>16);break;
    case 0x210:image.set_surface_word(2,surface_color_offset_);break;
    case 0x214:image.set_surface_word(3,surface_zeta_offset_);break;
    case 0x1d80:context_known_|=kContextCompression;break;
    case 0x1d8c:context_known_|=kContextClearZeta;break;
    case 0x1d90:context_known_|=kContextClearColor;break;
    case 0x1d98:context_known_|=kContextClearH;break;
    case 0x1d9c:context_known_|=kContextClearV;break;
    case 0x1e94:context_known_|=kContextTransformMode;break;
    case 0x1e9c:context_known_|=kContextProgramLoad;break;
    case 0x1ea0:context_known_|=kContextProgramStart;break;
    case 0x1ea4:context_known_|=kContextConstantLoad;break;
    case 0x1a4:context_semaphore_dma_known_|=uint8_t(1u<<sub);break;
    case 0x1d6c:context_semaphore_offset_known_|=uint8_t(1u<<sub);break;
    case 0x17c8:context_known_|=kContextZpassCount;break;
    case 0x17cc:context_known_|=kContextZpassEnable;break;
    default:break;
    }
}
