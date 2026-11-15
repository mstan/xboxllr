/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PILOT_DEPTH_TEXTURE_SHADER_H
#define PILOT_DEPTH_TEXTURE_SHADER_H
/* The conversion image copies the depth image's dimensions. Query that input:
 * the packaged native Linux compiler cannot lower RWTexture2D.GetDimensions.
 * Keep one production source for the renderer and compiler regression test. */
static const char pilot_depth_texture_source[]=
    "Texture2D<float4> depth_image:register(t0);\n"
    "Texture2D<uint4> stencil_image:register(t1);\n"
    "RWTexture2D<float4> argb_image:register(u0);\n"
    "[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {\n"
    " uint w,h; depth_image.GetDimensions(w,h); if(id.x>=w || id.y>=h)return;\n"
    " uint bits=asuint(depth_image.Load(int3(id.xy,0)).r); uint e=(bits>>23)&255;\n"
    " uint m=(bits&0x7fffff)|0x800000;\n"
    " uint d=e>=127?0xffffff:(e<103?0:((m>>(126-e))-(e>=126?1:0)));\n"
    " uint s=stencil_image.Load(int3(id.xy,0)).g;\n"
    " argb_image[id.xy]=float4((d>>8)&255,d&255,s,(d>>16)&255)/255.0;\n"
    "}\n";
#endif
