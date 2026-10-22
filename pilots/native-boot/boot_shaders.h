// SPDX-License-Identifier: MIT
// Original-look GPU programs adapted to HLSL from bennyborn/xbox-boot-re
// hosts/web/src/shaders.js at 12a3396aa77f72c8d2fd94c0988c7e11539b47e7.
// Copyright (c) 2026. MIT notice: src/third_party/xbox-boot-re/LICENSE.
// Workbench and enhanced rendering are deliberately absent.
#pragma once
static constexpr const char* BOOT_HLSL = R"hlsl(
cbuffer Draw : register(b0) {
    float4 c[20]; float4 k[4]; float4 bias; float4 misc;
};
Texture2D<float4> image0 : register(t0);
Texture2D<float4> image1 : register(t1);
Texture2D<float4> image2 : register(t2);
Texture2D<float4> image3 : register(t3);
TextureCube<float4> cube0 : register(t4);
Texture2D<float> light0 : register(t5);
SamplerState clamped : register(s0);
SamplerState repeated : register(s1);
struct V {
    float4 p : SV_Position;
    float4 d0 : COLOR0;
    float4 t0 : TEXCOORD0;
    float4 t1 : TEXCOORD1;
    float4 t2 : TEXCOORD2;
    float4 t3 : TEXCOORD3;
};
V none() {
    V o;
    o.p = 0; o.d0 = o.t0 = o.t1 = o.t2 = o.t3 = float4(0,0,0,1);
    return o;
}
float4 place(float4 p) {
    if (abs(p.w) < 1e-12) return p;
    float2 half_size = bias.zw;
    float2 screen = p.xy / p.w * float2(half_size.x, -half_size.y) + half_size + bias.x;
    float2 cut = trunc(screen * 16) / 16;
    return float4((cut - half_size) / float2(half_size.x, -half_size.y) * p.w, p.zw);
}
float4 transform(float4 v, int i) {
    return float4(dot(v,c[i]), dot(v,c[i+1]), dot(v,c[i+2]), dot(v,c[i+3]));
}
float rsq_abs(float x) { return rsqrt(abs(x)); }
float3 st(float3 x) { return clamp(x, -1, 1); }
float3 ex(float3 x) { return 2 * max(x,0) - 1; }
void alpha_test(float a) { if (misc.z != 0 && (int)round(a * 255) < (int)misc.x) discard; }
float light_tap(int2 p) {
    if (any(p < 0) || any(p >= 512)) return 1;
    return light0.Load(int3(p,0));
}
float shadow(float4 t) {
    if (misc.w == 0) return 1;
    if (t.w <= 0) return t.z < 0 ? 1 : 0;
    float2 p = t.xy / t.w - 0.5;
    int2 i = (int2)floor(p);
    float2 f = p - floor(p);
    float stored = lerp(lerp(light_tap(i),light_tap(i+int2(1,0)),f.x),
                        lerp(light_tap(i+int2(0,1)),light_tap(i+int2(1,1)),f.x),f.y) * 65535;
    return stored > clamp(t.z/t.w, 0, 65535) ? 1 : 0;
}
V smooth_vs(float3 v0 : POSITION, float3 a : NORMAL0, float3 b : NORMAL1, float3 n : NORMAL2) {
    float4 p = float4(v0,1);
    V o = none(); o.p = place(transform(p,0));
    o.t3 = float4(dot(p,c[8]),dot(p,c[9]),dot(p,c[10]),max(dot(p,c[11]),0));
    float3 q = v0*c[6].xyz, l = c[4].xyz-q, e = c[5].xyz-q;
    float l2 = dot(l,l), rl = rsq_abs(l2);
    float3 L = rl*l, H = rsq_abs(dot(e,e))*e+L;
    float attenuation = saturate(1 / dot(float3(1,l2*rl,l2),c[7].xyz));
    o.d0 = float4(attenuation,attenuation,attenuation,1);
    o.t1 = float4(dot(L,a),dot(L,b),dot(L,n),1);
    o.t2 = float4(dot(H,a),dot(H,b),dot(H,n),1);
    return o;
}
V relief_vs(float3 v0 : POSITION, float2 uv : TEXCOORD0, float3 a : NORMAL0, float3 b : NORMAL1, float3 n : NORMAL2) {
    V o = smooth_vs(v0,a,b,n); o.t0 = float4(uv,0,1); return o;
}
float4 chamber_ps(V v, bool bump) {
    float3 t1 = ex(cube0.SampleLevel(clamped,v.t1.xyz,0).rgb);
    float3 t2 = ex(cube0.SampleLevel(clamped,v.t2.xyz,0).rgb);
    float3 normal = bump ? ex(image0.SampleLevel(repeated,v.t0.xy/v.t0.w,0).rgb) : float3(0,0,1);
    float nl = clamp(dot(normal,t1),-1,1);
    float s = max(clamp(dot(normal,t2),-1,1),0);
    [unroll] for (int i=0;i<4;i++) s=s*s;
    if (!bump) s=s*s;
    float3 lit = st(k[1].rgb*max(nl,0)+k[2].rgb*s);
    float3 r0 = max(st(v.d0.rgb*lit+k[0].rgb),0);
    return float4(r0*(63.0/255.0)+r0*shadow(v.t3),1);
}
float4 smooth_ps(V v) : SV_Target { return chamber_ps(v,false); }
float4 relief_ps(V v) : SV_Target { return chamber_ps(v,true); }
float4 lightdepth_vs(float3 v0 : POSITION) : SV_Position { return place(transform(float4(v0,1),0)); }
V depth_vs(float3 v0 : POSITION) {
    float4 r=transform(float4(v0,1),0);
    V o=none(); o.p=place(r);
    o.d0=float4(saturate(max(c[16].xyz*(r.z+c[17].xyz),0)),1);
    o.t0=float4(r.xyz/r.w*c[18].xyz+c[19].xyz,1);
    return o;
}
float4 depth_ps(V v) : SV_Target {
    float3 e=ex(v.t0.rgb); float m=1-clamp(dot(e,e),0,1);
    alpha_test(1); return float4(max(st(v.d0.rgb*(m*m)),0),1);
}
V blob_vs(float3 v0 : POSITION, float4 v1 : NORMAL0) {
    float3 dir=v0*c[11].xyz;
    float4 q=float4(v0*c[10].xyz+c[12].xyz+rsq_abs(dot(dir,dir))*dir*v1.w,c[10].w+c[12].w+c[8].x*c[8].y);
    float3 eye=c[9].xyz-q.xyz,n=v1.xyz*c[11].xyz;
    V o=none(); o.p=place(transform(q,4));
    o.t1=float4(rsq_abs(dot(eye,eye))*eye,c[8].y);
    o.t0=float4(rsq_abs(dot(n,n))*n,c[8].y); return o;
}
V drop_vs(float3 v0 : POSITION) {
    float4 q=float4(c[12].xyz*v0+dot(v0,c[11].xyz)*c[13].xyz+c[10].xyz,c[8].y);
    float3 eye=c[9].xyz-q.xyz;
    V o=none();o.p=place(transform(q,4));o.t0=float4(v0,1);
    o.t1=float4(rsq_abs(dot(eye,eye))*eye,c[8].y);return o;
}
float4 blob_color(V v,bool drop) {
    float x=clamp(dot(ex(cube0.SampleLevel(clamped,v.t0.xyz,0).rgb),ex(cube0.SampleLevel(clamped,v.t1.xyz,0).rgb)),-1,1);
    float r=(1-clamp(x,0,1))*(1-clamp(x,0,1));
    float a=drop ? clamp((1-r)*k[0].a,-1,1)*k[2].a : k[0].a;
    return float4(st(st((1-r)*k[0].rgb)+k[1].rgb),a);
}
float4 blob_ps(V v) : SV_Target { return blob_color(v,false); }
float4 drop_ps(V v) : SV_Target { return blob_color(v,true); }
V veil_vs(float3 v0 : POSITION,float2 v1 : TEXCOORD0,float2 v2 : TEXCOORD1) {
    float4 s=float4(v2,0,1); V o=none();o.p=place(float4(v0,1));o.t0=float4(v1,0,1);
    o.t1=s*c[1]+c[0];o.t2=s*c[3]+c[2];o.t3=s*c[5]+c[4];return o;
}
float4 veil_ps(V v) : SV_Target {
    float3 t0=image0.SampleLevel(clamped,v.t0.xy/v.t0.w,0).rgb;
    float n=min(image1.SampleLevel(repeated,v.t1.xy/v.t1.w,0).r+image2.SampleLevel(repeated,v.t2.xy/v.t2.w,0).r,1);
    n=min(n+image3.SampleLevel(repeated,v.t3.xy/v.t3.w,0).r,1);
    alpha_test(1);return float4(st(st(t0*(n*k[0].rgb))+k[1].rgb),1);
}
V logod_vs(float3 v0 : POSITION,float2 v1 : TEXCOORD0) {
    float3 a=c[5].xyz+(c[4].xyz-c[5].xyz)*v1.y,b=c[7].xyz+(c[6].xyz-c[7].xyz)*v1.y;
    V o=none();o.p=place(transform(float4(v0,1),0));o.d0=float4(saturate(a*c[8].xyz+b-b*c[8].xyz),1);return o;
}
float4 logod_ps(V v) : SV_Target { return v.d0; }
V fixed_vs(float3 v0 : POSITION,float2 uv : TEXCOORD0) {
    V o=none();o.p=place(transform(float4(v0,1),0));o.t0=float4(uv,0,1);return o;
}
float4 tap(int2 p) {
    uint w,h;image0.GetDimensions(w,h);int2 size=int2(w,h);
    if (((uint)misc.y&8u)!=0 && (any(p<0)||any(p>=size))) return k[2];
    return image0.Load(int3(clamp(p,int2(0,0),size-1),0));
}
float4 fixed_ps(V v) : SV_Target {
    uint w,h;image0.GetDimensions(w,h);uint mode=(uint)misc.y;
    float2 p=v.t0.xy/v.t0.w*float2(w,h)-0.5,f=p-floor(p);int2 i=(int2)floor(p);
    float4 t=lerp(lerp(tap(i),tap(i+int2(1,0)),f.x),lerp(tap(i+int2(0,1)),tap(i+int2(1,1)),f.x),f.y);
    float4 outc=float4(1,1,1,k[3].a);
    if ((mode&1u)!=0) outc.rgb*=t.rgb;
    if ((mode&2u)!=0) outc.rgb*=k[3].rgb;
    if ((mode&4u)!=0) outc.a*=t.a;
    alpha_test(outc.a);return outc;
}
V plain_vs(float3 v0 : POSITION) { V o=none();o.p=place(transform(float4(v0,1),0));return o; }
float4 plain_ps(V v) : SV_Target { return k[3]; }
float4 present_vs(uint i : SV_VertexID) : SV_Position {
    float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[i],0,1);
}
float4 present_ps(float4 p : SV_Position) : SV_Target {
    int2 at=int2((int)p.x*2,(int)p.y);
    return (image0.Load(int3(at,0))+image0.Load(int3(at+int2(1,0),0)))*0.5;
}
float4 window_ps(float4 p : SV_Position) : SV_Target {
    return image0.SampleLevel(clamped,(p.xy-bias.xy)/bias.zw,0);
}
)hlsl";
