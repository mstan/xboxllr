/* Optional host raster scale. Guest resource metadata stays at its original
 * size. CPU access crosses an explicit resampling boundary; ordinary sampled
 * textures are untouched. All calls share the graphics owner's lock. */
static struct PilotVisualMods pilot_visual={1,0,1,0,1,0,0,4,3};
static uint32_t pilot_guest_width,pilot_guest_height,pilot_guest_viewport[6];
int pilot_gfx_visual_mods(const struct PilotVisualMods *options)
{
    if(pilot_device || !pilot_visual_mods_validate(options))return 0;
    /* Cadence implementations are separately qualified before this gate opens.
     * A parseable request must never silently become an ineffective FPS mod. */
    if(options->frame_mode==PILOT_FRAMES_ACTUAL)return 0;
#ifndef PILOT_D3D_PHOBOS
    if(options->internal_height || options->supersample!=1 || options->antialiasing ||
       options->anisotropy!=1 || options->frame_mode)return 0;
#endif
    pilot_visual=*options;
#ifdef PILOT_D3D_PHOBOS
    void pilot_d3d_visual_policy_changed(void);pilot_d3d_visual_policy_changed();
#endif
    return 1;
}
void pilot_d3d_scene_size(unsigned width,unsigned height,unsigned *out_width,unsigned *out_height)
{
    uint32_t size[2];
    if(!pilot_visual_mods_size(&pilot_visual,width,height,width,height,16384,size))
        pilot_d3d_backend_stop("visual mod scene dimensions exceed native capability");
    pilot_guest_width=width;pilot_guest_height=height;
    memset(pilot_guest_viewport,0,sizeof(pilot_guest_viewport));
    pilot_guest_viewport[2]=width;pilot_guest_viewport[3]=height;pilot_guest_viewport[5]=0x3f800000u;
    uint32_t axis=pilot_presentation_axis(&pilot_presentation);
    if(size[0]>16384/axis || size[1]>16384/axis)pilot_d3d_backend_stop("guest raster dimensions exceed native capability");
    *out_width=size[0]*axis;*out_height=size[1]*axis;
    if(size[0]!=width || size[1]!=height)
        fprintf(stderr,"[VISUAL MOD] guest=%ux%u render=%ux%u ssaa_axis=%u aa=%u aniso=%u\n",
            width,height,size[0],size[1],pilot_visual.supersample,pilot_visual.antialiasing,pilot_visual.anisotropy);
}
unsigned pilot_d3d_anisotropy(void){return pilot_visual.anisotropy;}
void pilot_d3d_target_size(unsigned usage,unsigned width,unsigned height,unsigned *out_width,unsigned *out_height)
{
    *out_width=width;*out_height=height;
    if((usage&2) && pilot_depth_allocation_size) {
        *out_width=pilot_depth_allocation_size[0];*out_height=pilot_depth_allocation_size[1];return;
    }
    if(!(usage&3) || (!pilot_visual.internal_height && pilot_visual.supersample==1))return;
    uint32_t size[2];
    /* Ordinary sampled lookup allocations retain their native-size policy.
     * Standalone depth follows its bound color target in pilot_gfx_targets. */
    int valid=pilot_visual_mods_target_size(&pilot_visual,pilot_guest_width,pilot_guest_height,width,height,16384,size);
    if(!valid)
        pilot_d3d_backend_stop("visual mod target dimensions exceed native capability");
    *out_width=size[0];*out_height=size[1];
}
int pilot_gfx_render_size(uint32_t size[2])
{
    if(!pilot_device || !size)return 0;
    size[0]=d3d8_GetBackbufferWidth();size[1]=d3d8_GetBackbufferHeight();return 1;
}
/* Native CPU writes to scaled color RTs are uncommon (locks/save images), not
 * a per-draw or per-frame image conversion. Resample only after the donor has
 * converted the original texel layout. Depth uses its explicit staging path. */
#ifdef PILOT_D3D_PHOBOS
int pilot_d3d_upload_scaled(RhiImage *image,unsigned level,unsigned width,unsigned height,const void *data,unsigned pitch)
{
    RhiImageDesc desc;rhi_image_get_desc(image,&desc);
    unsigned physical_width=desc.width>>level,physical_height=desc.height>>level;
    if(!physical_width)physical_width=1;if(!physical_height)physical_height=1;
    if(physical_width==width && physical_height==height)return 0;
    if(!width || !height || desc.mip_levels!=1 ||
       (desc.format!=RHI_FORMAT_R8G8B8A8_UNORM && desc.format!=RHI_FORMAT_B8G8R8A8_UNORM) || pitch<(uint64_t)width*4)
        pilot_d3d_backend_stop("unsupported scaled CPU color upload");
    size_t bytes=(size_t)desc.width*desc.height*4;
    unsigned char *pixels=malloc(bytes);if(!pixels)pilot_d3d_backend_stop("scaled upload allocation failed");
    for(unsigned y=0;y<desc.height;++y) {
        const unsigned char *row=(const unsigned char*)data+(size_t)(((uint64_t)y*2+1)*height/(desc.height*2ull))*pitch;
        for(unsigned x=0;x<desc.width;++x)
            memcpy(pixels+((size_t)y*desc.width+x)*4,row+(size_t)(((uint64_t)x*2+1)*width/(desc.width*2ull))*4,4);
    }
    rhi_image_update(image,0,NULL,pixels,desc.width*4,desc.width*desc.height*4);free(pixels);return 1;
}
#endif
static int pilot_visual_scaled(void)
{return pilot_visual.internal_height || pilot_visual.supersample!=1;}
static unsigned pilot_visual_sample(unsigned coordinate,unsigned logical,unsigned physical)
{
    /* Logical pixel centers, including nonintegral scales. Never read the
     * padding beyond a row or reinterpret the guest's byte pitch as a host one. */
    return (unsigned)(((uint64_t)coordinate*2+1)*physical/((uint64_t)logical*2));
}
#include "graphics_visual_present.inc"
