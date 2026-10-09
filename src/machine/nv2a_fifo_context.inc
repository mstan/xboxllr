// Authored CACHE1 register transport. Wire fields: nxdk outer.h and envytools
// rnndb/fifo/nv4_pfifo.xml. The original driver saves these fields to RAMFC;
// they must describe the actual parser, not detached MMIO write shadows.
bool Nv2a::pfifo_context_read(uint32_t offset, uint32_t& value) {
    const auto& ch=channels_[pfifo_regs_[0x1204]&31];
    switch(offset) {
    case 0x1228:
        value=(ch.non_incrementing?1u:0u)|(ch.method&0x1ffc)|
              (uint32_t(ch.subchannel)<<13)|(ch.method_count<<18);return true;
    case 0x122c:value=ch.dma_instance>>4;return true;
    case 0x1240:value=ch.put;return true;
    case 0x1244:value=ch.get;return true;
    case 0x1248:value=ch.reference;return true;
    case 0x124c:value=ch.subroutine|(ch.subroutine_active?1u:0u);return true;
    case 0x1254:value=ch.last_engine;return true;
    case 0x1280:value=ch.engines;return true;
    // PFIFO semaphore acquisition is not implemented. Its reset state is
    // inactive, and activation/programming is rejected below and at dispatch.
    // These are not NV097's separate, implemented PGRAPH semaphore methods.
    case 0x1260:case 0x1264:case 0x1268:case 0x126c:value=0;return true;
    default:return false;
    }
}

bool Nv2a::pfifo_context_write(uint32_t offset, uint32_t value) {
    auto& ch=channels_[pfifo_regs_[0x1204]&31];
    switch(offset) {
    case 0x1228:
        if(value&~0x1ffcfffdU){fault_("NV2A unsupported PFIFO parser error/state restore");return true;}
        ch.non_incrementing=(value&1)!=0;ch.method=value&0x1ffc;
        ch.subchannel=(value>>13)&7;ch.method_count=(value>>18)&0x7ff;
        return true;
    case 0x1248:ch.reference=value;return true;
    case 0x124c:
        if(value&2){fault_("NV2A invalid PFIFO subroutine state");return true;}
        ch.subroutine=value&~3u;ch.subroutine_active=(value&1)!=0;return true;
    case 0x1254:
        if(value>1){fault_("NV2A unsupported PFIFO last engine");return true;}
        ch.last_engine=value;return true;
    case 0x1280:
        if(value&~0x11111111u){fault_("NV2A unsupported PFIFO engine routing");return true;}
        ch.engines=value;return true;
    case 0x1260:case 0x1264:case 0x1268:case 0x126c:
        if(value)fault_("NV2A PFIFO semaphore acquisition is not implemented");
        return true;
    default:return false;
    }
}
