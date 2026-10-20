#ifndef PILOT_GRAPHICS_PRESENTATION_H
#define PILOT_GRAPHICS_PRESENTATION_H
#include <stdint.h>

/* Guest allocation/description policy, independent of optional host scaling.
 * 2222 is supersampling: four raster texels, not four host MSAA samples. */
typedef struct PilotPresentation {
    uint32_t width,height,sample_type,depth,flags;
    uint32_t color_format,copy_buffers;
} PilotPresentation;
static inline int pilot_presentation_parse(const uint32_t p[17],PilotPresentation *out)
{
    if(!p || !out || !p[0] || !p[1] || p[0]>4096 || p[1]>4096)return 0;
    int copy=p[5]==3;
    if((p[2]!=6 && p[2]!=7 && p[2]!=0x11 && p[2]!=0x12 && p[2]!=0x1e) ||
       (p[3]!=1 && !(copy && p[3]==2)) ||
       (p[4]!=0x11 && p[4]!=0x2222) || (p[5]!=1 && !copy) ||
       (copy && p[4]!=0x11) || p[6] || p[7] || p[8]>1 ||
       (p[8]?(p[9]!=0x2a && p[9]!=0x2e):p[9]!=0) ||
       (p[10]&~0x170u) || (p[10]&0x60u)==0x60u ||
       (p[11] && p[11]!=60) || (p[12]!=1 && p[12]!=0x80000000u) ||
       p[13] || p[14] || p[15] || p[16])return 0;
    /* Xbox linear Size has twelve-bit dimensions and eight-bit pitch/64.
     * Check raster headers too, before allocating native or guest resources. */
    uint32_t axis=p[4]==0x2222?2:1;
    if(p[0]>4096/axis || p[1]>4096/axis || ((p[0]*axis*4+63)&~63u)>16384)return 0;
    out->width=p[0];out->height=p[1];out->sample_type=p[4];out->depth=p[8];out->flags=p[10];
    out->color_format=p[2];out->copy_buffers=copy?p[3]:0;return 1;
}
static inline unsigned pilot_presentation_mask(const PilotPresentation *p)
{return (p->depth?7u:3u)|(p->copy_buffers==2?8u:0u);}
static inline int pilot_presentation_profile_admits(const uint32_t p[17],unsigned version)
{
    PilotPresentation parsed;
    if(version>2 || !pilot_presentation_parse(p,&parsed))return 0;
    if(version<2 && (p[2]==0x11 || p[3]!=1 || p[5]!=1 || (p[10]&0x100u)))return 0;
    return version || (p[4]==0x11 && p[8]==1 && p[10]==0);
}
static inline uint32_t pilot_presentation_axis(const PilotPresentation *p)
{return p->sample_type==0x2222?2u:1u;}
static inline int pilot_presentation_surface(const PilotPresentation *p,unsigned index,uint32_t size[3])
{
    if(index>=4 || !(pilot_presentation_mask(p)&(1u<<index)))return 0;
    uint32_t axis=(index==1 || index==3)?1:pilot_presentation_axis(p);
    unsigned bytes=index!=2 && p->color_format==0x11?2:4;
    size[0]=p->width*axis;size[1]=p->height*axis;size[2]=(size[0]*bytes+63)&~63u;return 1;
}
static inline uint32_t pilot_presentation_samples(const PilotPresentation *p,unsigned index)
{return index==0?p->sample_type:0x11u;}
#endif
