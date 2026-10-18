"""Explicit mod-owned raster policy for the pinned private native donor.

No inherited donor environment/config setting may enable an enhancement behind
the resolved mod plan. Metadata/CPU layouts stay logical; RT GPU images scale.
"""
def once(code, old, new):
    if code.count(old)!=1:
        raise ValueError('unreviewed visual donor boundary: '+old[:100])
    return code.replace(old,new)


def prepare_visual(code, name):
    if name=='d3d8_device.c':
        # Reset's guest dimensions must not survive a later fresh device.
        # This initialization repair applies to original rendering as well.
        code=once(code,'    state->present_width = dd.width;',
            '    g_guest_width=dd.width;g_guest_height=dd.height;\n'
            '    state->present_width = dd.width;')
        code=once(code,'#include "d3d8_display.h"','#include "d3d8_display.h"\n'
            'int32_t pilot_d3d_visual_present(void);\n'
            'HRESULT pilot_d3d_visual_resolve(RhiView*,unsigned,unsigned,RhiView*,D3D8DisplayFit,unsigned,unsigned);')
        code=once(code,'    d3d8_display_resolve(s->rhi_scene_srv, s->width, s->height, rhi_swapchain_view(), fit);',
            '    pilot_d3d_visual_resolve(s->rhi_scene_srv,s->width,s->height,rhi_swapchain_view(),fit,s->swap_width,s->swap_height);')
        code=once(code,'    return (HRESULT)rhi_present(0);','    return (HRESULT)pilot_d3d_visual_present();')
    if name=='d3d8_display.c':
        start=code.index('const D3D8DisplayPolicy *d3d8_display_policy(void)')
        end=code.index('\n}',start)+2
        code=code[:start]+'''unsigned pilot_d3d_anisotropy(void);
const D3D8DisplayPolicy *d3d8_display_policy(void)
{
    if(!g_policy_read) {
        memset(&g_policy,0,sizeof(g_policy));g_policy_read=1;
        g_policy.scale=1;g_policy.anisotropy=pilot_d3d_anisotropy();
    }
    return &g_policy;
}
void pilot_d3d_visual_policy_changed(void){g_policy_read=0;}'''+code[end:]
        start=code.index('void d3d8_display_scene_size(')
        end=code.index('\n}',start)+2
        code=code[:start]+'''void pilot_d3d_scene_size(unsigned,unsigned,unsigned*,unsigned*);
void d3d8_display_scene_size(UINT guest_w,UINT guest_h,UINT *scene_w,UINT *scene_h)
{pilot_d3d_scene_size(guest_w,guest_h,scene_w,scene_h);}'''+code[end:]
    if name=='d3d8_resources.c':
        code=once(code,'    tex->image = rhi_image_create(&td, NULL);',
            '    pilot_d3d_target_size(Usage,Width,Height,&td.width,&td.height);\n'
            '    tex->image = rhi_image_create(&td, NULL);')
        code=once(code,'    rhi_image_update(image, subresource, NULL, upload_data, upload_pitch, upload_pitch * rows);',
            '    if(!pilot_d3d_upload_scaled(image,subresource,w,h,upload_data,upload_pitch))\n'
            '        rhi_image_update(image, subresource, NULL, upload_data, upload_pitch, upload_pitch * rows);')
        include='#include "d3d8_internal.h"'
        code=once(code,include,include+'\n'
            'void pilot_d3d_target_size(unsigned,unsigned,unsigned,unsigned*,unsigned*);\n'
            'int pilot_d3d_upload_scaled(RhiImage*,unsigned,unsigned,unsigned,const void*,unsigned);')
    return code
