// Authored original-driver control plane. nxdk/pbkit supplies the Xbox RAM
// mirror/RDI map; envytools supplies independent register and internal-memory
// observations. Unimplemented modes/engines remain explicit faults.
namespace {
struct PgraphMode { uint32_t offset, value; };
// Supported fixed NV2A pipeline configuration. These are hardware settings,
// not title addresses. The software pipeline is coherent (no stale hardware
// caches), with the configured checks/ordinary filtering paths. Other debug
// modes, silicon workarounds and fault-injection settings need qualification.
constexpr PgraphMode pgraph_modes[] = {
    {0x084,0x00118700}, {0x08c,0xf3de0479}, {0x090,0}, {0x094,4},
    {0x098,0x78}, {0x09c,0x40}, {0x880,0x0028c3ff},
    {0xb80,0x45ead10e}, {0xb84,0}, {0xb88,0},
};
// Byte offsets within the RDI 0xEA RAM configuration bank.
int pgraph_ram_offset(uint32_t offset) {
    if (offset >= 0x900 && offset < 0x980 && (offset & 15) < 12) {
        return int(0x10 + ((offset-0x900)/16)*4 + ((offset&15)/4)*0x20);
    }
    if (offset >= 0x980 && offset < 0x9a0) return int(offset-0x980+0x90);
    if (offset == 0x9a0) return 0x0c;
    if (offset == 0x9a4) return 0;
    if (offset == 0x9a8) return 4;
    return -1;
}
int pfb_ram_offset(uint32_t offset) {
    if (offset >= 0x240 && offset < 0x2c0 && (offset & 15) < 12)
        return int(0x10 + ((offset-0x240)/16)*4 + ((offset&15)/4)*0x20);
    if (offset >= 0x300 && offset < 0x320) return int(offset-0x300+0x90);
    if (offset == 0x324) return 0x0c;
    return -1;
}
}

uint32_t* Nv2a::ram_config_word(RamTileConfig& config, uint32_t offset) {
    if (offset & 3) return nullptr;
    if (offset == 0) return &config.config0;
    if (offset == 4) return &config.config1;
    if (offset == 12) return &config.zcomp_offset;
    if (offset >= 0x10 && offset < 0x70)
        return &config.tiles[((offset-0x10)%0x20)/4][(offset-0x10)/0x20];
    if (offset >= 0x90 && offset < 0xb0) return &config.zcomp[(offset-0x90)/4];
    return nullptr;
}

bool Nv2a::ram_config_write(RamTileConfig& config,uint32_t offset,uint32_t value) {
    auto* word=ram_config_word(config,offset);
    if (!word) { fault_("NV2A unsupported RAM configuration word"); return false; }
    if ((offset == 0 && value != 0x03070003) || (offset == 4 && value != 0) ||
        (offset == 12 && value != 0)) {
        fault_("NV2A RAM geometry/compression mode is not implemented"); return false;
    }
    // Original NV2A uses NV20 ZCOMP fields. Only little-endian Z24S8 is
    // implemented; Z16/endian/offset modes must not select a different codec.
    if(offset>=0x90 && value && ((value&~0x8403ffc0u) ||
                                 (value&0x84000000u)!=0x84000000u)) {
        fault_("NV2A unsupported ZCOMP format/flags");return false;
    }
    if (offset >= 0x10 && offset < 0x30 && (value & ~0x03ffc003u)) {
        fault_("NV2A unsupported tile address/flags"); return false;
    }
    // MMIO register programming precedes access. The driver can write base,
    // limit and pitch in either order; validate the complete configuration at
    // the memory operation. Never change the layout under an older draw job.
    if (offset >= 0x10 && pgraph_status()) {
        fault_("NV2A tile reconfiguration while graphics busy is unqualified");return false;
    }
    *word=value;return true;
}

bool Nv2a::ram_tiles_active() const {
    for (const auto* config : {&pfb_tiles_,&pgraph_tiles_,&texture_tiles_})
        for (const auto& tile : config->tiles) if (tile[0]&1) return true;
    return false;
}

bool Nv2a::cpu_aperture(uint32_t offset,void* data,size_t bytes,bool write) {
    if(draw_faulted_)return false; // preserve the first draw fault
    if(!data || (bytes!=1 && bytes!=2 && bytes!=4 && bytes!=8) ||
       uint64_t(offset)+bytes>0x04000000u) {
        fault_("NV2A invalid CPU aperture transaction");return false;
    }
    if(renderer_) {
        fault_("NV2A CPU aperture renderer residency is unqualified");return false;
    }
    // Do not infer a linear CPU view from the GPU's tiled-client translator.
    // Inspect every client bank, including incomplete reconfiguration epochs.
    for(const auto* config:{&pfb_tiles_,&pgraph_tiles_,&texture_tiles_})
        for(const auto& tile:config->tiles) {
            if(!(tile[0]&1))continue;
            const uint32_t base=tile[0]&0x03ffc000u;
            if(tile[1]<base || tile[1]>=0x04000000u) {
                fault_("NV2A CPU aperture has an invalid active tile extent");return false;
            }
            if(offset<=tile[1] && uint64_t(offset)+bytes>base) {
                fault_("NV2A CPU aperture overlaps unqualified tiled storage");return false;
            }
        }
    // Retire only already-issued host draw work, as other CPU memory consumers
    // do. This does not pull pending PFIFO commands or advance device time.
    draw_queue_drain_all();
    if(draw_faulted_)return false; // a delayed draw fault forbids the CPU transfer
    const bool ok=write ? mem_write_ && mem_write_(offset,data,bytes)
                        : mem_read_ && mem_read_(offset,data,bytes);
    if(!ok)fault_("NV2A CPU aperture has no owned RAM extent");
    return ok;
}

bool Nv2a::ram_tile_overlap(uint32_t address,size_t bytes) const {
    if(!bytes)return false;
    for(const auto& tile:pgraph_tiles_.tiles)
        if((tile[0]&1) && address<=tile[1] &&
           uint64_t(address)+bytes>(tile[0]&0x03ffc000u))return true;
    return false;
}

bool Nv2a::ram_tile_configuration() {
    for (unsigned i=0;i<8;++i) {
        const auto& tile=pgraph_tiles_.tiles[i];
        if (!((tile[0]|pfb_tiles_.tiles[i][0]|texture_tiles_.tiles[i][0])&1))continue;
        // The qualified original/hardware-test path programs matching banks.
        // Different client views need separate evidence, not implicit aliases.
        if(tile!=pfb_tiles_.tiles[i] || tile!=texture_tiles_.tiles[i]) {
            fault_("NV2A tile RAM-client banks disagree");return false;
        }
        if(pgraph_tiles_.zcomp[i]!=pfb_tiles_.zcomp[i] ||
           pgraph_tiles_.zcomp[i]!=texture_tiles_.zcomp[i]) {
            fault_("NV2A compression RAM-client banks disagree");return false;
        }
        Nv2aTileLayout layout;
        const uint32_t base=tile[0]&0x03ffc000u;
        if(tile[1]<base || tile[1]>=0x04000000u ||
           !layout.configure(tile[2],(tile[0]>>1)&1)) {
            fault_("NV2A invalid active tile extent/pitch");return false;
        }
        for(unsigned j=0;j<i;++j) {
            const auto& other=pgraph_tiles_.tiles[j];
            if((other[0]&1) && base<=other[1] && (other[0]&0x03ffc000u)<=tile[1]) {
                fault_("NV2A overlapping active tiles are unqualified");return false;
            }
        }
    }
    return true;
}

int Nv2a::ram_tile_at(uint32_t address) const {
    for(unsigned i=0;i<8;++i) {
        const auto& tile=pgraph_tiles_.tiles[i];
        if((tile[0]&1) && address>=(tile[0]&0x03ffc000u) && address<=tile[1])return int(i);
    }
    return -1;
}

bool Nv2a::ram_tile_transfer(uint32_t address,void* data,size_t bytes,bool write) {
    if(!bytes)return true;
    if(uint64_t(address)+bytes>0x04000000u)return false;
    const int selected=ram_tile_at(address);
    if(selected<0) {
        // Entering another tile partway through a transfer is not covered by
        // the hardware fixture. Do not silently treat those bytes as linear.
        for(const auto& tile:pgraph_tiles_.tiles)
            if((tile[0]&1) && address<(tile[0]&0x03ffc000u) &&
               uint64_t(address)+bytes>(tile[0]&0x03ffc000u))return false;
        return write ? mem_write_ && mem_write_(address,data,bytes)
                     : mem_read_ && mem_read_(address,data,bytes);
    }
    const auto& tile=pgraph_tiles_.tiles[unsigned(selected)];
    const uint32_t base=tile[0]&0x03ffc000u;
    if(uint64_t(address)+bytes>uint64_t(tile[1])+1)return false;
    Nv2aTileLayout layout;
    if(!layout.configure(tile[2],(tile[0]>>1)&1))return false;
    // Physical storage stays unchanged. Translate each naturally contiguous
    // 16-byte run; this also handles unaligned and subpixel transfers.
    for(size_t offset=0;offset<bytes;) {
        uint32_t linear=address-base+uint32_t(offset),physical=0;
        const size_t span=std::min(bytes-offset,size_t(16-(linear&15)));
        if(pgraph_tiles_.zcomp[unsigned(selected)]&0x80000000u) {
            ZetaBlock block;std::array<uint32_t,16> pixels;
            if(!ram_zeta_block(unsigned(selected),linear,block) || !ram_zeta_read(block,pixels))
                return false;
            auto* pixel_bytes=reinterpret_cast<uint8_t*>(pixels.data());
            const uint32_t in_block=((linear/tile[2])&3)*16+(linear&15);
            auto* buffer=static_cast<uint8_t*>(data)+offset;
            if(write) {
                std::memcpy(pixel_bytes+in_block,buffer,span);
                // Ordinary memory/2D writes expand the block and clear its
                // tag. ZROP compression is selected separately below.
                if(!ram_zeta_write(block,pixels,false))return false;
            } else std::memcpy(buffer,pixel_bytes+in_block,span);
            offset+=span;continue;
        }
        if(!layout.translate(linear,physical) || uint64_t(base)+physical+span>uint64_t(tile[1])+1)
            return false;
        auto* buffer=static_cast<uint8_t*>(data)+offset;
        if(write ? !mem_write_ || !mem_write_(base+physical,buffer,span)
                 : !mem_read_ || !mem_read_(base+physical,buffer,span))return false;
        offset+=span;
    }
    return true;
}

uint32_t Nv2a::pgraph_status() const {
    // Coarse engine-busy indication; per-unit busy bits are not qualified.
    // A prefetched PFIFO queue is not an active PGRAPH engine. The original
    // driver disables its puller, waits for this engine to idle, then changes
    // context. Engine idle is not proof that queued FIFO commands completed.
    return (pgraph_executing_ || waiting_for_nop_ || waiting_for_flip_ ||
            draw_published_ != draw_retired_) ? 1 : 0;
}

void Nv2a::pgraph_reset_setup() {
    pgraph_setup_.fill(0);pgraph_context_switch_.fill(0);
    pgraph_setup_mode_initialized_=0;
    pgraph_context_managed_=false;
    pgraph_executing_=false;
    pgraph_context_control_=pgraph_context_user_=pgraph_ffintfc_=pgraph_rdi_index_=0;
    pgraph_context_pointer_=0;pgraph_context_pointer_initialized_=false;
    pgraph_context_reset_seen_=pgraph_context_cache_invalidated_=false;
    pgraph_loaded_context_.reset();
    pgraph_method_faulted_=false;
    pgraph_fd_state_.fill(0);pgraph_fd_initialized_=0;
    dxt1_noise_control_.fill(0);
    pgraph_tiles_=RamTileConfig{};texture_tiles_=RamTileConfig{};
    compress_zbuffer_=false; // controller tags are not PGRAPH context state
    invalidate_vsh_program_cache();
}

bool Nv2a::pgraph_load_context() {
    // The admitted original first-context route disables the puller and waits
    // for the actual engine, then resets the subengines/clears FD, invalidates
    // caches, selects its instance and loads. Prefetched packets are permitted;
    // neither their GET position nor engine idle means they were executed.
    if(pgraph_loaded_context_) {
        fault_("NV2A replacement context load requires qualified state preservation");return false;
    }
    if(!(pmc_enable_&(1u<<12)) || !pgraph_context_managed_ ||
       (pgraph_context_control_&~0x100u)!=0x10000000u || pgraph_context_user_!=0 ||
       pgraph_setup_[0x084/4]!=0x00118700u || pgraph_setup_[0x880/4]!=0x0028c3ffu ||
       pgraph_setup_mode_initialized_!=((1u<<(sizeof(pgraph_modes)/sizeof(*pgraph_modes)))-1)) {
        fault_("NV2A context load configuration is unsupported");return false;
    }
    if(pgraph_fifo_access_ || pgraph_status() || draw_faulted_) {
        fault_("NV2A context load requires an idle engine and disabled FIFO");return false;
    }
    if(!pgraph_context_reset_seen_ || pgraph_setup_[0x080/4] ||
       !pgraph_context_cache_invalidated_ || pgraph_fd_initialized_!=0x7fff ||
       primitive_mode_ || !vertex_indices_.empty() || !inline_array_words_.empty() ||
       !inline_vertices_.empty() || !immediate_vertices_.empty() || methods_.total()) {
        fault_("NV2A context load lacks the original first-context reset preparation");return false;
    }
    if(!pgraph_context_pointer_initialized_) {
        fault_("NV2A context pointer has not been programmed");return false;
    }
    const uint32_t base=pgraph_context_pointer_<<4;
    if(uint64_t(base)+Nv2aContextImage::kSourceBytes>pramin_.size()) {
        fault_("NV2A context image exceeds instance RAM");return false;
    }
    for(size_t i=base;i<base+Nv2aContextImage::kSourceBytes;++i) {
        if(!(pramin_initialized_[i/64]&(uint64_t(1)<<(i%64)))) {
            fault_("NV2A context image contains uninitialized instance bytes");return false;
        }
    }
    auto staged=std::make_unique<Nv2aContextImage>();
    const char* error=nullptr;
    if(!staged->load(pramin_.data()+base,Nv2aContextImage::kSourceBytes,error)) {
        fault_(error);return false;
    }

    // Commit only after every admission/encoding check passes. Restore actual
    // measured fields directly, not via method handlers with extra side effects.
    // Primary surface slots: DST0, SRC1, COLOR2, ZETA3, DVD4, SWIZZLED5.
    context_surfaces_2d_.dest_offset=staged->surface_word(0);
    context_surfaces_2d_.source_offset=staged->surface_word(1);
    context_surfaces_2d_.dest_pitch=staged->surface_word(12);
    context_surfaces_2d_.source_pitch=staged->surface_word(13);
    surface_color_offset_=staged->surface_word(2);
    surface_zeta_offset_=staged->surface_word(3);
    surface_pitch_=staged->surface_word(14)|(staged->surface_word(15)<<16);
    raster_state_[kRasterSurfaceColorOffset]=surface_color_offset_;
    raster_state_[kRasterSurfaceZetaOffset]=surface_zeta_offset_;
    raster_state_[kRasterSurfacePitch]=surface_pitch_;
    // Do not infer PRAMIN object identities/bound flags from resolved BASE/LIMIT.
    // The rest of the live frontend is unknown after LOAD, not reset-to-zero.
    // Its method/read consumers remain guarded until explicit provenance exists.
    pgraph_loaded_context_=std::move(staged);
    pgraph_method_faulted_=false;
    context_known_=0;
    context_objects_known_=context_semaphore_dma_known_=context_semaphore_offset_known_=0;
    context_raster_known_.fill(0);
    context_raster_known_[kRasterSurfaceColorOffset]=0xffffffffu;
    context_raster_known_[kRasterSurfaceZetaOffset]=0xffffffffu;
    context_raster_known_[kRasterSurfacePitch]=0xffffffffu;
    context_program_known_.fill(0);context_constants_known_.fill(0);
    context_vertex_format_known_=context_vertex_offset_known_=0;
    context_viewport_scale_known_=context_viewport_offset_known_=0;
    // Bound flags represent provenance, not loaded register values. Resolved
    // raw BASE/LIMIT cannot establish a descriptor identity or its ownership.
    color_dma_bound_=zeta_dma_bound_=texture_dma_a_bound_=texture_dma_b_bound_=false;
    vertex_dma_a_bound_=vertex_dma_b_bound_=false;
    context_surfaces_2d_.dma_source_bound=context_surfaces_2d_.dma_dest_bound=false;
    image_blit_.context_surfaces_bound=false;report_dma_bound_.fill(false);
    pgraph_context_user_=0x11;
    pgraph_fd_initialized_=0;
    invalidate_vsh_program_cache();
    // No CHID_VALID, FIFO release, packet retirement or completion is supplied.
    // The original following CONTROL write owns channel admission. Dynamic
    // CONTROL readback is not inferred from one observed10100000 value.
    return true;
}

uint32_t* Nv2a::dxt1_noise_control_word(uint32_t select,uint32_t address) {
    // Corroborated by nxdk pbkit PB_SETNOISE and the unmodified original
    // XDK handler. Other words in these banks are not generic register RAM.
    if(select==0xe0 && address==0x50)return &dxt1_noise_control_[0];
    if(select==0xdf && address==8)return &dxt1_noise_control_[1];
    return nullptr;
}

bool Nv2a::pgraph_setup_read(uint32_t offset,uint32_t& value) {
    for(const auto& mode:pgraph_modes)if(offset==mode.offset){value=pgraph_setup_[offset/4];return true;}
    int ram_offset=pgraph_ram_offset(offset);
    if(ram_offset>=0){value=*ram_config_word(pgraph_tiles_,uint32_t(ram_offset));return true;}
    if(offset>=0x820 && offset<=0x878) {
        if(!pgraph_loaded_context_ || !pgraph_loaded_context_->surface_known((offset-0x820)/4)) {
            fault_("NV2A surface register has no established context value");value=0;return true;
        }
        value=pgraph_loaded_context_->surface_word((offset-0x820)/4);return true;
    }
    if(offset>=0x14c && offset<=0x158) {
        if(pgraph_loaded_context_) {
            fault_("NV2A loaded graph-object read requires qualified context mapping");value=0;return true;
        }
        value=pgraph_context_switch_[(offset-0x14c)/4];return true;
    }
    switch(offset) {
    case 0x080:value=pgraph_setup_[offset/4];return true;
    case 0x144:
        if(pgraph_loaded_context_) {
            fault_("NV2A loaded context-control dynamic readback is unqualified");value=0;return true;
        }
        value=pgraph_context_control_;return true;
    case 0x148:value=pgraph_context_user_;return true;
    case 0x700:value=pgraph_status();return true;
    case 0x750:value=pgraph_rdi_index_;return true;
    case 0x754: {
        uint32_t select=(pgraph_rdi_index_>>16)&0x1ff,address=pgraph_rdi_index_&0x1ffc;
        if(pgraph_loaded_context_ && select!=0xea) {
            fault_("NV2A loaded RDI read requires qualified context mapping");value=0;return true;
        }
        if(select==0x3d) {
            if(address>=pgraph_fd_state_.size()*4 ||
               !(pgraph_fd_initialized_&(1u<<(address/4)))) {
                fault_("NV2A FD state read has no initialized word");value=0;return true;
            }
            value=pgraph_fd_state_[address/4];
            pgraph_rdi_index_=(pgraph_rdi_index_&~0x1ffcu)|((address+4)&0x1ffc);return true;
        }
        auto* word=select==0xea?ram_config_word(texture_tiles_,address)
                             :dxt1_noise_control_word(select,address);
        if(!word){fault_("NV2A unsupported RDI read bank/address");value=0;return true;}
        value=*word;pgraph_rdi_index_=(pgraph_rdi_index_&~0x1ffcu)|((address+4)&0x1ffc);return true;
    }
    case 0x764:value=pgraph_ffintfc_;return true;
    case 0x784:
        if(!pgraph_context_pointer_initialized_) {
            fault_("NV2A context pointer has not been programmed");value=0;return true;
        }
        value=pgraph_context_pointer_;return true;
    case 0x710:case 0xb10:case 0x186c:case 0x1a88:
        if(pgraph_loaded_context_) {
            const uint64_t needed=offset==0x710 ? kContextFlipRead|kContextFlipWrite|kContextFlipModulo :
                offset==0xb10 ? kContextPatternColor : offset==0x186c ? kContextClearColor : kContextClearZeta;
            if((context_known_&needed)!=needed) {
                fault_("NV2A loaded frontend read requires qualified context mapping");value=0;return true;
            }
        }
        return false;
    default:return false;
    }
}

bool Nv2a::pgraph_setup_write(uint32_t offset,uint32_t value) {
    for(const auto& mode:pgraph_modes)if(offset==mode.offset) {
        // DEBUG_1 cache-invalidate is a command bit, not persistent state.
        uint32_t configured=offset==0x084?value&~0x01000000u:value;
        // Kernel boot/nxdk use DEBUG_7=0008CFFF; the original title initializer
        // explicitly programs0028C3FF. Differing bits10/11/21 are not decoded
        // by the inspected primary sources. Preserve boot register storage,
        // with execution still guarded in drain_pull_queue/batching. This does
        // not assert that the two modes draw alike or invent names for the bits.
        const bool boot_debug7=offset==0x880 && configured==0x0008cfffu;
        if(configured!=mode.value && !boot_debug7){fault_("NV2A unsupported PGRAPH pipeline configuration");return true;}
        if(offset==0x880 && configured!=pgraph_setup_[offset/4] && pgraph_status()) {
            fault_("NV2A DEBUG_7 change while graphics busy is unqualified");return true;
        }
        if(offset==0x084 && (value&0x01000000u)) {
            invalidate_vsh_program_cache();pgraph_context_cache_invalidated_=true;
        }
        pgraph_setup_[offset/4]=configured;
        pgraph_setup_mode_initialized_|=uint16_t(1u<<(&mode-pgraph_modes));return true;
    }
    int ram_offset=pgraph_ram_offset(offset);
    if(ram_offset>=0){ram_config_write(pgraph_tiles_,uint32_t(ram_offset),value);return true;}
    if(offset>=0x820 && offset<=0x878) {
        fault_("NV2A direct surface write requires qualified context validity effects");return true;
    }
    if(offset>=0x14c && offset<=0x158) {
        // Context-switch registers describe the active graph object. Only
        // clearing an unbound object is qualified here; arbitrary latching
        // would leave class/DMA state different from the visible registers.
        if(pgraph_loaded_context_ || value || std::any_of(object_class_.begin(),object_class_.end(),[](uint8_t c){return c!=0;}))
            fault_("NV2A graph-object context transfer is not implemented");
        else pgraph_context_switch_[(offset-0x14c)/4]=0;
        return true;
    }
    switch(offset) {
    case 0x080:
        if(value&~0x70000u || (value && (pgraph_loaded_context_ || methods_.total() || pgraph_status()))) {
            fault_("NV2A active/unsupported PGRAPH subengine reset");return true;
        }
        if(value) {
            vertex_indices_.clear();inline_array_words_.clear();inline_vertices_.clear();immediate_vertices_.clear();
            primitive_mode_=0;invalidate_vsh_program_cache();
            // A subengine reset does not establish FD SRAM contents.
            pgraph_fd_initialized_=0;
            pgraph_context_reset_seen_=value==0x70000;
            pgraph_context_cache_invalidated_=false;
        }
        pgraph_setup_[offset/4]=value;return true;
    case 0x144:
        if(value&~0x10010103u){fault_("NV2A unsupported context-control mode");return true;}
        pgraph_context_managed_=true;pgraph_context_control_=value;return true;
    case 0x148:
        if(pgraph_loaded_context_) {
            // The reached channel-zero rewrite preserves the measured low11.
            // Other channel/header profiles have not been transferred here.
            if(value!=0x11)fault_("NV2A loaded context-user rewrite is unsupported");
            else pgraph_context_user_=value;
            return true;
        }
        if(value&~0x1f000000u){fault_("NV2A unsupported context-user mode");return true;}
        pgraph_context_user_=value;return true;
    case 0x784:
        // nxdk outer.h CHANNEL_CTX_POINTER_INST: sixteen-bit PRAMIN
        // instance, with byte address instance<<4. Programming the pointer
        // neither transfers context nor grants the pending channel access.
        if(value&~0xffffu){fault_("NV2A unsupported context pointer bits");return true;}
        pgraph_context_pointer_=value;pgraph_context_pointer_initialized_=true;return true;
    case 0x788:
        // SAVE needs the inverse live-state mapping, including state changed
        // by methods. Copying the opaque source image back would be a false save.
        if(value==1)pgraph_load_context();
        else if(value==2)fault_("NV2A PGRAPH context save is not implemented");
        else fault_("NV2A unsupported PGRAPH context transfer command");
        return true;
    case 0x71c:
        return false; // guarded known flip inputs are checked in the actual handler
    case 0xb10:case 0x186c:case 0x1a88:
        if(pgraph_loaded_context_) {
            fault_("NV2A loaded frontend write requires qualified context mapping");return true;
        }
        return false;
    case 0x750:
        if(value&~0x01ff1ffcu){fault_("NV2A invalid RDI index");return true;}
        pgraph_rdi_index_=value;return true;
    case 0x754: {
        uint32_t select=(pgraph_rdi_index_>>16)&0x1ff,address=pgraph_rdi_index_&0x1ffc;
        if(select==0x3d) {
            // envytools f102b823 pgraph_state.cc maps bank3D to fifteen
            // FD patch-state words. Original13FC4C clears all fifteen while
            // PGRAPH is idle, before context loading. Active patch commands
            // remain explicit faults; no tessellation state is discarded.
            if(address>=pgraph_fd_state_.size()*4 || value || pgraph_status()) {
                fault_("NV2A unsupported active/nonzero FD state write");return true;
            }
            pgraph_fd_state_[address/4]=0;pgraph_fd_initialized_|=uint16_t(1u<<(address/4));
            pgraph_rdi_index_=(pgraph_rdi_index_&~0x1ffcu)|((address+4)&0x1ffc);return true;
        }
        if(auto* word=dxt1_noise_control_word(select,address)) {
            if(value>1 || pgraph_status()) {
                fault_("NV2A unsupported DXT1 noise control transition");return true;
            }
            *word=value;
            pgraph_rdi_index_=(pgraph_rdi_index_&~0x1ffcu)|((address+4)&0x1ffc);
            return true;
        }
        if(select!=0xea){fault_("NV2A unsupported RDI write bank");return true;}
        if(ram_config_write(texture_tiles_,address,value))
            pgraph_rdi_index_=(pgraph_rdi_index_&~0x1ffcu)|((address+4)&0x1ffc);
        return true;
    }
    case 0x764:
        if(value&~0x38000000u){fault_("NV2A unsupported FIFO interface control");return true;}
        pgraph_ffintfc_=value;
        if(!(value&0x30000000u))resume_pushbuffers();
        return true;
    default:return false;
    }
}
