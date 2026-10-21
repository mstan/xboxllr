/* Shared native lighting, applied to the prepared transform/texture VS result.
 * Lighting equations adapt the Phobos fixed-function shader identified in
 * THIRD_PARTY_ATTRIBUTION.md; this remains the same GPL-dependent reference
 * renderer. Original light/material ownership and selector mapping are separate.
 * Original caches select material sources. Light positions and directions are
 * uploaded in view space, matching the original packet owner's view transform.
 * Exact NV2A intermediate precision is a separately documented deferral. */
static const char native_lighting_source[]=
"cbuffer NativeLightingCB : register(b4) {\n"
" float4 NativeAmbient,NativeMatDiffuse,NativeMatAmbient,NativeMatSpecular,NativeMatEmissive;\n"
" float4 NativeLightParams; uint4 NativeMaterialSources; uint4 NativeLightCount;\n"
" struct {float4 positionType,directionRange,diffuse,ambient,specular,attenuation,spot;} NativeLights[8];\n"
"};\n"
"float4 native_material(uint source,float4 material,VS_IN v) {\n"
" if(source==1 && (Flags&FLAG_HAS_DIFFUSE))return v.diffuse.bgra;\n"
" if(source==2 && (Flags&FLAG_HAS_SPECULAR))return v.specular.bgra;\n"
" return material;}\n"
"VS_OUT native_lighting(VS_IN v,VS_OUT o) {\n"
" if(!(Flags&FLAG_LIGHTING))return o;\n"
" float3 p=mul(float4(v.pos.xyz,1),WorldView).xyz;\n"
" float3 n=mul(v.normal,(float3x3)WorldViewInvTranspose);\n"
" if(NativeLightParams.z!=0 && dot(n,n)>0)n=normalize(n);\n"
" float3 eye=NativeLightParams.y!=0?normalize(-p):float3(0,0,-1);\n"
" float4 md=native_material(NativeMaterialSources.x,NativeMatDiffuse,v);\n"
" float4 ms=native_material(NativeMaterialSources.y,NativeMatSpecular,v);\n"
" float4 ma=native_material(NativeMaterialSources.z,NativeMatAmbient,v);\n"
" float4 me=native_material(NativeMaterialSources.w,NativeMatEmissive,v);\n"
" float3 diffuse=me.rgb+ma.rgb*NativeAmbient.rgb,specular=0;\n"
" for(uint i=0;i<NativeLightCount.x;i++){\n"
"  float3 l;float attenuation=1;uint type=(uint)NativeLights[i].positionType.w;\n"
"  if(type==3)l=-normalize(NativeLights[i].directionRange.xyz);\n"
"  else {float3 d=NativeLights[i].positionType.xyz-p;float distance=length(d);\n"
"   if(distance>NativeLights[i].directionRange.w)continue;\n"
"   l=distance>0?d/distance:float3(0,0,0);\n"
"   float denominator=dot(NativeLights[i].attenuation.xyz,float3(1,distance,distance*distance));\n"
"   attenuation=1/max(denominator,1e-30);\n"
"   if(type==2){float cone=dot(-l,normalize(NativeLights[i].directionRange.xyz));\n"
"    float outer=NativeLights[i].spot.y,inner=NativeLights[i].spot.x;\n"
"    float ramp=inner==outer?(cone>=inner?1:0):saturate((cone-outer)/(inner-outer));\n"
"    attenuation*=cone<outer?0:pow(ramp,NativeLights[i].attenuation.w);}}\n"
"  float ndotl=max(dot(n,l),0);\n"
"  diffuse+=attenuation*(ma.rgb*NativeLights[i].ambient.rgb+ndotl*md.rgb*NativeLights[i].diffuse.rgb);\n"
"  if(NativeLightParams.w!=0 && ndotl>0){float3 halfVector=l+eye;\n"
"   float ndoth=dot(halfVector,halfVector)>0?max(dot(n,normalize(halfVector)),0):0;\n"
"   specular+=attenuation*pow(ndoth,NativeLightParams.x)*ms.rgb*NativeLights[i].specular.rgb;}\n"
" }\n"
" o.diffuse=float4(saturate(diffuse),md.a);o.specular=float4(saturate(specular),0);return o;}\n";
