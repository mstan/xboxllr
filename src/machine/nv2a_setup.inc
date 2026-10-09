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
        (offset == 12 && value != 0) || (offset >= 0x90 && value != 0)) {
        fault_("NV2A RAM geometry/compression mode is not implemented"); return false;
    }
    // The original Xbox driver uses base|1 for color and base|3 for depth.
    // nxdk's historical base|2 setup is not an enable-bit specification.
    // Until memory-client translation is connected, neither flag variant
    // may silently admit identity-addressed access to a tiled surface.
    if (offset >= 0x10 && offset < 0x30 && (value & 3)) {
        fault_("NV2A active tile addressing needs RAM-client qualification"); return false;
    }
    *word=value;return true;
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
    pgraph_tiles_=RamTileConfig{};texture_tiles_=RamTileConfig{};
    invalidate_vsh_program_cache();
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
        auto* word=select==0xea?ram_config_word(texture_tiles_,address):nullptr;
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
        if(configured!=mode.value){fault_("NV2A unsupported PGRAPH pipeline configuration");return true;}
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
