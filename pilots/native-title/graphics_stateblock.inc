/* Xbox state blocks are owned CPU snapshots, not native GPU command lists.
 * Serialize the original record layout, including resource references. The
 * native renderer does not execute the original driver to capture state. */
#ifndef PILOT_D3D_STATEBLOCK_AVAILABLE
/* Previously emitted headers describe the complete reviewed state-block ABI. */
#define PILOT_D3D_STATEBLOCK_AVAILABLE 1
#endif
#define SNAPSHOT_FIELD(name) (PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_##name)

static void graphics_snapshot_init(void)
{
    /* The reviewed original starts with zeroed device/stream storage, FVF XYZ
     * and ten identity transforms. Other initial GPU work is not a snapshot. */
    memset(memory+SNAPSHOT_FIELD(TEXTURES),0,16);
    MEM32(SNAPSHOT_FIELD(PIXEL_SHADER))=0;MEM32(SNAPSHOT_FIELD(VERTEX_SHADER))=2;
    MEM32(SNAPSHOT_FIELD(VERTEX_OBJECT))=0;
    MEM32(SNAPSHOT_FIELD(INDICES))=0;MEM32(SNAPSHOT_FIELD(BASE_VERTEX))=0;
#ifdef PILOT_D3D_SNAPSHOT_LIGHTS
    MEM32(SNAPSHOT_FIELD(LIGHTS))=0;
#endif
#ifdef PILOT_D3D_SNAPSHOT_LIGHT_COUNT
    MEM32(SNAPSHOT_FIELD(LIGHT_COUNT))=0;
#endif
    memset(memory+PILOT_D3D_STREAMS,0,16*12);
#ifdef PILOT_D3D_SNAPSHOT_PIXEL_CONSTANTS
    memset(memory+SNAPSHOT_FIELD(PIXEL_CONSTANTS),0,16*16);
#endif
    memset(memory+graphics_vertex_constants_address(),0,192*16);
#ifdef PILOT_D3D_SNAPSHOT_MATERIALS
    memset(memory+SNAPSHOT_FIELD(MATERIALS),0,2*68);
#endif
    uint32_t *transforms=graphics_pointer(SNAPSHOT_FIELD(TRANSFORMS),10*64);
    memset(transforms,0,10*64);
    for(unsigned i=0;i<10;++i)for(unsigned j=0;j<4;++j)transforms[i*16+j*5]=0x3f800000;
}
#if PILOT_D3D_STATEBLOCK_AVAILABLE
struct GraphicsStateBlock {
    uint32_t header,bytes,refs[23],kinds[23],count;
};
static struct GraphicsStateBlock graphics_blocks[128];
static void graphics_block_ref(uint32_t kind,uint32_t header,int acquire)
{
    if(kind==0)graphics_texture_ref(graphics_texture_slot(header),acquire);
    else if(kind==3)graphics_vertex_ref(header,acquire);
    else if(kind==4)graphics_index_ref(header,acquire);
    else {
        int owned=0;
        for(unsigned i=0;i<256;++i)
            if(kind==1?graphics_pixels[i]==header:graphics_shaders[i].header==header)owned=1;
        if(!owned || !MEM32(header))stop("unowned state-block shader reference",header);
        if(acquire) {
            if(MEM32(header)==UINT32_MAX)stop("state-block shader reference overflow",header);
            ++MEM32(header);
        } else {
            /* The creator still owns the shader until its delete service.
             * A future bound delete implementation must share this ownership. */
            if(MEM32(header)<2)stop("state-block shader reference underflow",header);
            --MEM32(header);
        }
    }
}
static void graphics_block_release(unsigned slot)
{
    struct GraphicsStateBlock *b=&graphics_blocks[slot];
    for(unsigned i=0;i<b->count;++i)graphics_block_ref(b->kinds[i],b->refs[i],0);
    uint32_t bytes=0;
    if(vm_free(&b->header,&bytes,MEM_RELEASE))stop("state-block storage release failed",slot);
    memset(b,0,sizeof(*b));
}
static void graphics_blocks_release(void)
{for(unsigned i=0;i<128;++i)if(graphics_blocks[i].header)graphics_block_release(i);}

static void graphics_block_capture(uint32_t type,const struct GraphicsStateBlock *block)
{
    const char *directory=getenv("XLLE_PILOT_STATEBLOCK_CAPTURE");
    if(!directory || !*directory)return;
    static unsigned sequence;char path[1024];
    if(snprintf(path,sizeof(path),"%s/state-%u.bin",directory,++sequence)>=(int)sizeof(path))stop("state-block capture path too long",0);
    uint32_t header[]={0x53424c58,1,type,PILOT_D3D_SNAPSHOT_VERTEX_CONSTANTS+192*16,192,166*4,4*128,block->bytes};
    FILE *file=fopen(path,"wb");
    if(!file || fwrite(header,sizeof(header),1,file)!=1 ||
       fwrite(memory+PILOT_D3D_DEVICE_OBJECT,header[3],1,file)!=1 ||
       fwrite(memory+PILOT_D3D_STREAMS,header[4],1,file)!=1 ||
       fwrite(memory+PILOT_D3D_RENDER_STATES,header[5],1,file)!=1 ||
       fwrite(memory+PILOT_D3D_TEXTURE_STATES,header[6],1,file)!=1 ||
       fwrite(memory+block->header,block->bytes,1,file)!=1 || fclose(file))stop("state-block capture write failed",0);
}

static uint32_t graphics_block_create(const uint32_t *args)
{
    uint32_t type=args[0];
    if(type<1 || type>3)stop("invalid state-block type",type);
    graphics_pointer(args[1],4);MEM32(args[1])=0;
    unsigned slot;for(slot=0;slot<128 && graphics_blocks[slot].header;++slot){}
    if(slot==128)return 0x8007000eu;
    if(MEM32(SNAPSHOT_FIELD(LIGHT_COUNT)))stop("state capture with unqualified light bindings",MEM32(SNAPSHOT_FIELD(LIGHT_COUNT)));
    uint32_t viewport[6];
    if(pilot_gfx_get_viewport(viewport)<0)stop("state-block viewport query failed",0);
    memcpy(memory+SNAPSHOT_FIELD(VIEWPORT),viewport,sizeof(viewport));
    uint8_t mask[540]={0};
    if(type==1)memset(mask,0xff,sizeof(mask));
    else if(type==2) {
        mask[4]=1;memset(mask+23,1,16);
        for(unsigned i=0;i<sizeof(pilot_d3d_block_pixel_render);++i)mask[231+pilot_d3d_block_pixel_render[i]]=1;
        for(unsigned i=0;i<sizeof(pilot_d3d_block_pixel_texture);++i)
            for(unsigned s=0;s<4;++s)mask[397+pilot_d3d_block_pixel_texture[i]*4+s]=1;
    } else {
        mask[5]=1;memset(mask+39,1,192);
        for(unsigned i=0;i<sizeof(pilot_d3d_block_vertex_render);++i)mask[231+pilot_d3d_block_vertex_render[i]]=1;
        for(unsigned i=0;i<sizeof(pilot_d3d_block_vertex_texture);++i)
            for(unsigned s=0;s<4;++s)mask[397+pilot_d3d_block_vertex_texture[i]*4+s]=1;
    }
    uint32_t data[2048]={0},cursor=15;struct GraphicsStateBlock block={0};
    uint32_t pixel=MEM32(SNAPSHOT_FIELD(PIXEL_SHADER)),vertex=MEM32(SNAPSHOT_FIELD(VERTEX_SHADER));
    if((vertex&1) && MEM32(SNAPSHOT_FIELD(VERTEX_OBJECT))!=vertex-1)stop("inconsistent state-block shader binding",vertex);
    /* Reference entries are kept separately from caller-visible serialized
     * bytes, so guest edits cannot redirect destruction to arbitrary objects. */
#define KEEP(kind,value) do {uint32_t resource=(value);if(resource){if(block.count==23)stop("state-block reference capacity",resource);block.kinds[block.count]=(kind);block.refs[block.count++]=resource;}}while(0)
#define WORD(v) do {if(cursor==2048)stop("state-block capture capacity",cursor);data[cursor++]=(v);}while(0)
#define COPY(a,n) do {unsigned words=(n);if(words>2048-cursor)stop("state-block capture capacity",cursor);memcpy(data+cursor,graphics_pointer((a),words*4),words*4);cursor+=words;}while(0)
    for(unsigned i=0;i<4;++i)if(mask[i]){uint32_t h=MEM32(SNAPSHOT_FIELD(TEXTURES)+i*4);++data[0];WORD(i);WORD(h);KEEP(0,h);}
    if(mask[4]){data[1]=1;WORD(pixel);KEEP(1,pixel);}
    if(mask[5]){data[2]=1;WORD(vertex);if(vertex&1)KEEP(2,vertex-1);}
    if(mask[6]){uint32_t h=MEM32(SNAPSHOT_FIELD(INDICES));data[3]=1;WORD(MEM32(SNAPSHOT_FIELD(BASE_VERTEX)));WORD(h);KEEP(4,h);}
    for(unsigned i=0;i<16;++i)if(mask[7+i]){uint32_t h=MEM32(PILOT_D3D_STREAMS+i*12+8);++data[4];WORD(i);WORD(MEM32(PILOT_D3D_STREAMS+i*12));WORD(h);KEEP(3,h);}
    for(unsigned i=0;i<16;++i)if(mask[23+i] && pixel){++data[5];WORD(i);COPY(SNAPSHOT_FIELD(PIXEL_CONSTANTS)+i*16,4);}
    /* Original capture excludes the first 96 reserved vertex constants. */
    for(unsigned i=96;i<192;++i)if(mask[39+i]){++data[6];WORD(i);COPY(SNAPSHOT_FIELD(VERTEX_CONSTANTS)+i*16,4);}
    for(unsigned i=0;i<166;++i)if(mask[231+i] && (pixel || (i>56 && i!=136))){++data[7];WORD(i);WORD(MEM32(PILOT_D3D_RENDER_STATES+i*4));}
    for(unsigned i=0;i<32;++i)for(unsigned s=0;s<4;++s)if(mask[397+i*4+s]){++data[8];WORD(s);WORD(i);WORD(MEM32(PILOT_D3D_TEXTURE_STATES+s*128+i*4));}
    for(unsigned i=0;i<10;++i)if(mask[525+i]){++data[9];WORD(i);COPY(SNAPSHOT_FIELD(TRANSFORMS)+i*64,16);}
    if(mask[535]){data[10]=1;COPY(SNAPSHOT_FIELD(VIEWPORT),6);}
    for(unsigned i=0;i<2;++i)if(mask[536+i]){data[11+i]=1;COPY(SNAPSHOT_FIELD(MATERIALS)+i*68,17);}
#undef KEEP
#undef WORD
#undef COPY
    block.bytes=cursor*4;uint32_t allocation=block.bytes;
    if(vm_allocate(&block.header,0,&allocation,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return 0x8007000eu;
    memcpy(memory+block.header,data,block.bytes);
    for(unsigned i=0;i<block.count;++i)graphics_block_ref(block.kinds[i],block.refs[i],1);
    graphics_blocks[slot]=block;MEM32(args[1])=block.header;
    graphics_block_capture(type,&block);
    fprintf(stderr,"[STATE BLOCK] type=%u token=%08x bytes=%u refs=%u\n",type,block.header,block.bytes,block.count);
    return 0;
}
#else
/* No state-block resources can be created through an unavailable boundary. */
static void graphics_blocks_release(void) {}
static uint32_t graphics_block_create(const uint32_t *args)
{
    (void)args;stop("state-block binding is unqualified for this profile",0);
    return 0;
}
#endif
#undef SNAPSHOT_FIELD
