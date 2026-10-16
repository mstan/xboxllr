// Authored compressed-memory clients. Format/codec attribution is retained in
// nv2a_zeta_compression.hpp; tag mapping in nv2a_tile_layout.hpp.
bool Nv2a::ram_zeta_block(unsigned index,uint32_t linear,ZetaBlock& block) {
    const auto& tile=pgraph_tiles_.tiles[index];
    const uint32_t base=tile[0]&0x03ffc000u,pitch=tile[2];
    Nv2aTileLayout layout;
    if(!layout.configure(pitch,(tile[0]>>1)&1))return false;
    uint32_t partition=0,tag=0;
    if(!layout.compression_tag(linear,partition,tag))return false;
    const uint64_t absolute_tag=uint64_t(tag)+(pgraph_tiles_.zcomp[index]&0x3ffc0u);
    // Bounded address field, not a claim about NV2A silicon tag capacity or
    // its debug-port layout. Neither is supplied by the NV20 codec reference.
    if(absolute_tag>=0x40000u) {fault_("NV2A compression tag address exceeds supported range");return false;}
    block.key=uint32_t(absolute_tag)*4+partition;
    const uint64_t start=uint64_t((linear/pitch)&~3u)*pitch+((linear%pitch)&~15u);
    for(unsigned y=0;y<4;++y) {
        uint32_t physical=0;
        const uint64_t row=start+uint64_t(y)*pitch;
        if(row+16>uint64_t(tile[1])-base+1 || row>UINT32_MAX ||
           !layout.translate(uint32_t(row),physical) ||
           uint64_t(base)+physical+16>uint64_t(tile[1])+1) {
            fault_("NV2A compression block exceeds tile extent");return false;
        }
        block.rows[y]=base+physical;
    }
    return true;
}

bool Nv2a::ram_zeta_read(const ZetaBlock& block,std::array<uint32_t,16>& pixels) {
    const auto tag=zeta_compression_tags_.find(block.key);
    if(tag==zeta_compression_tags_.end()) {
        fault_("NV2A compressed tile read requires initialized tag state");return false;
    }
    auto read=[&](uint32_t address,void* data,size_t bytes) {
        if(mem_read_ && mem_read_(address,data,bytes))return true;
        fault_("NV2A compression backing read failed");return false;
    };
    if(tag->second) {
        Nv2aZetaCompression::Encoded encoded{};
        if(!read(block.rows[0],encoded.data(),8))return false;
        if(!(encoded[7]&0x80) && !read(block.rows[0]+8,encoded.data()+8,8))return false;
        Nv2aZetaCompression::decode(encoded,pixels);
    } else for(unsigned y=0;y<4;++y)
        if(!read(block.rows[y],pixels.data()+y*4,16))return false;
    return true;
}

bool Nv2a::ram_zeta_write(const ZetaBlock& block,const std::array<uint32_t,16>& pixels,bool compress) {
    Nv2aZetaCompression::Encoded encoded{};
    const unsigned size=compress?Nv2aZetaCompression::encode(pixels,encoded):0;
    auto write=[&](uint32_t address,const void* data,size_t bytes) {
        if(mem_write_ && mem_write_(address,data,bytes))return true;
        fault_("NV2A compression backing write failed");return false;
    };
    // A failed/partial bus write cannot attach an old valid tag to changed
    // bytes. Only successful data publication establishes new tag state.
    zeta_compression_tags_.erase(block.key);
    if(size) {
        if(!write(block.rows[0],encoded.data(),size))return false;
    } else for(unsigned y=0;y<4;++y)
        if(!write(block.rows[y],pixels.data()+y*4,16))return false;
    zeta_compression_tags_[block.key]=size!=0;return true;
}

bool Nv2a::ram_zeta_clear(unsigned index,uint32_t first,uint32_t pitch,uint32_t width,
                         uint32_t height,uint32_t value,uint32_t mask) {
    if(pgraph_loaded_context_ && !(context_known_&kContextCompression)) {
        fault_("NV2A loaded compressed clear selector is unknown");return false;
    }
    const auto& tile=pgraph_tiles_.tiles[index];
    const uint32_t base=tile[0]&0x03ffc000u;
    if(pitch!=tile[2] || first<base || (first&3)) {
        fault_("NV2A compressed clear surface/tile layout disagrees");return false;
    }
    const uint32_t xmin=((first-base)%pitch)/4,ymin=(first-base)/pitch;
    const uint64_t xmax=uint64_t(xmin)+width,ymax=uint64_t(ymin)+height;
    if(xmax*4>pitch || !height || uint64_t(first)+uint64_t(height-1)*pitch+width*4>uint64_t(tile[1])+1) {
        fault_("NV2A compressed clear exceeds tile extent");return false;
    }
    for(uint32_t y=ymin&~3u;y<ymax;y+=4)for(uint32_t x=xmin&~3u;x<xmax;x+=4) {
        ZetaBlock block;std::array<uint32_t,16> pixels;
        if(!ram_zeta_block(index,y*pitch+x*4,block))return false;
        const bool complete=x>=xmin && uint64_t(x)+4<=xmax && y>=ymin && uint64_t(y)+4<=ymax;
        if(complete && mask==0xffffffffu)pixels.fill(value);
        else {
            if(!ram_zeta_read(block,pixels))return false;
            for(uint32_t by=0;by<4;++by)for(uint32_t bx=0;bx<4;++bx)
                if(x+bx>=xmin && x+bx<xmax && y+by>=ymin && y+by<ymax)
                    pixels[by*4+bx]=(pixels[by*4+bx]&~mask)|(value&mask);
        }
        if(!ram_zeta_write(block,pixels,compress_zbuffer_))return false;
    }
    return true;
}
