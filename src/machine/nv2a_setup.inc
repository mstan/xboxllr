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
    pgraph_context_managed_=false;
    pgraph_executing_=false;
    pgraph_context_control_=pgraph_context_user_=pgraph_ffintfc_=pgraph_rdi_index_=0;
    pgraph_fd_state_.fill(0);pgraph_fd_initialized_=0;
    dxt1_noise_control_.fill(0);
    pgraph_tiles_=RamTileConfig{};texture_tiles_=RamTileConfig{};
    compress_zbuffer_=false; // controller tags are not PGRAPH context state
    invalidate_vsh_program_cache();
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
    if(offset>=0x14c && offset<=0x158){value=pgraph_context_switch_[(offset-0x14c)/4];return true;}
    switch(offset) {
    case 0x080:value=pgraph_setup_[offset/4];return true;
    case 0x144:value=pgraph_context_control_;return true;
    case 0x148:value=pgraph_context_user_;return true;
    case 0x700:value=pgraph_status();return true;
    case 0x750:value=pgraph_rdi_index_;return true;
    case 0x754: {
        uint32_t select=(pgraph_rdi_index_>>16)&0x1ff,address=pgraph_rdi_index_&0x1ffc;
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
    default:return false;
    }
}

bool Nv2a::pgraph_setup_write(uint32_t offset,uint32_t value) {
    for(const auto& mode:pgraph_modes)if(offset==mode.offset) {
        // DEBUG_1 cache-invalidate is a command bit, not persistent state.
        uint32_t configured=offset==0x084?value&~0x01000000u:value;
        // The original boot driver and nxdk program DEBUG_7=0008CFFF.
        // Preserve its register state, but do not execute commands using it:
        // it changes fragment limit checks (among other unqualified bits).
        // drain_pull_queue and its batching path enforce that boundary. This is
        // transport support, not an assertion that both modes draw alike.
        const bool boot_debug7=offset==0x880 && configured==0x0008cfffu;
        if(configured!=mode.value && !boot_debug7){fault_("NV2A unsupported PGRAPH pipeline configuration");return true;}
        if(offset==0x880 && configured!=pgraph_setup_[offset/4] && pgraph_status()) {
            fault_("NV2A DEBUG_7 change while graphics busy is unqualified");return true;
        }
        if(offset==0x084 && (value&0x01000000u))invalidate_vsh_program_cache();
        pgraph_setup_[offset/4]=configured;return true;
    }
    int ram_offset=pgraph_ram_offset(offset);
    if(ram_offset>=0){ram_config_write(pgraph_tiles_,uint32_t(ram_offset),value);return true;}
    if(offset>=0x14c && offset<=0x158) {
        // Context-switch registers describe the active graph object. Only
        // clearing an unbound object is qualified here; arbitrary latching
        // would leave class/DMA state different from the visible registers.
        if(value || std::any_of(object_class_.begin(),object_class_.end(),[](uint8_t c){return c!=0;}))
            fault_("NV2A graph-object context transfer is not implemented");
        else pgraph_context_switch_[(offset-0x14c)/4]=0;
        return true;
    }
    switch(offset) {
    case 0x080:
        if(value&~0x70000u || (value && (methods_.total() || pgraph_status()))) {
            fault_("NV2A active/unsupported PGRAPH subengine reset");return true;
        }
        if(value) {
            vertex_indices_.clear();inline_array_words_.clear();inline_vertices_.clear();immediate_vertices_.clear();
            primitive_mode_=0;invalidate_vsh_program_cache();
            // A subengine reset does not establish FD SRAM contents.
            pgraph_fd_initialized_=0;
        }
        pgraph_setup_[offset/4]=value;return true;
    case 0x144:
        if(value&~0x10010103u){fault_("NV2A unsupported context-control mode");return true;}
        pgraph_context_managed_=true;pgraph_context_control_=value;return true;
    case 0x148:
        if(value&~0x1f000000u){fault_("NV2A unsupported context-user mode");return true;}
        pgraph_context_user_=value;return true;
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
