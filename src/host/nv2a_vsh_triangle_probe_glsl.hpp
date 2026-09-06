#pragma once
#include <string>
#include "nv2a_vsh_dot_probe_fp64_glsl.hpp"

namespace xlle::vsh_shadow_probe {

// Diagnostic second dispatch consumes the first dispatch's raw output SSBO.
// Each candidate owns one slot: no unordered atomic append or CPU readback
// between VSH and triangle setup. The caller commits no raster destination.
inline const std::string kTriangleComputeSource = std::string(R"GLSL(#version 430 core
layout(local_size_x=64) in;
struct Output { uvec4 values[8]; };
layout(std430,binding=2) readonly buffer Vertices { Output vertices[]; };
layout(std430,binding=3) readonly buffer Statuses { uint statuses[]; };
layout(std430,binding=4) readonly buffer References { uint refs[]; };
layout(std430,binding=5) writeonly buffer Triangles { uvec4 triangles[]; };
uniform uint u_reference_count;
uniform uint u_primitive;
uniform uint u_large_target;
uniform uint u_cull_enabled;
uniform uint u_cull_front;
)GLSL") + std::string(kFp32ArithmeticSource) + R"GLSL(
bool finite4(uvec4 v) { return all(lessThan(v & uvec4(0x7fffffffu), uvec4(0x7f800000u))); }
bool valid_vertex(uint i) {
    uvec4 p=vertices[i].values[7];
    return finite4(p) && ((p.w & 0x7fffffffu)!=0u) && finite4(vertices[i].values[1]);
}
dvec2 xy(uint i) {
    uvec4 p=vertices[i].values[7];
    return dvec2(decode32(p.x),decode32(p.y));
}
// llround halfway-away-from-zero, with range rejection before any integer
// conversion. Integer-valued doubles stay exact for the bounded fixed area.
dvec2 fixed_xy(dvec2 v) { return sign(v)*floor(abs(v*16.0)+0.5); }
void main() {
    uint tid=gl_GlobalInvocationID.x;
    uint count=u_primitive==5u ? u_reference_count/3u :
        (u_reference_count>=3u ? u_reference_count-2u : 0u);
    if (tid>=count) return;
    uvec3 source;
    if (u_primitive==5u) source=uvec3(tid*3u,tid*3u+1u,tid*3u+2u);
    else if (u_primitive==6u) source=(tid & 1u)==0u ?
        uvec3(tid,tid+1u,tid+2u) : uvec3(tid+1u,tid,tid+2u);
    else source=uvec3(0u,tid+1u,tid+2u);
    uvec3 ids=uvec3(refs[source.x],refs[source.y],refs[source.z]);
    if ((statuses[ids.x] | statuses[ids.y] | statuses[ids.z])!=0u) {
        triangles[tid]=uvec4(0u,0u,0u,3u); return;
    }
    if (!valid_vertex(ids.x) || !valid_vertex(ids.y) || !valid_vertex(ids.z)) {
        triangles[tid]=uvec4(0u,0u,0u,2u); return;
    }
    bvec3 behind=bvec3((vertices[ids.x].values[7].w & 0x80000000u)!=0u,
        (vertices[ids.y].values[7].w & 0x80000000u)!=0u,
        (vertices[ids.z].values[7].w & 0x80000000u)!=0u);
    if (all(behind)) { triangles[tid]=uvec4(0u); return; }
    if (any(behind)) { triangles[tid]=uvec4(0u,0u,0u,2u); return; }
    precise dvec2 a=xy(ids.x), b=xy(ids.y), c=xy(ids.z);
    if (u_large_target==0u) {
        a=fixed_xy(a); b=fixed_xy(b); c=fixed_xy(c);
        if (any(greaterThan(abs(a),dvec2(1000000.0))) ||
            any(greaterThan(abs(b),dvec2(1000000.0))) ||
            any(greaterThan(abs(c),dvec2(1000000.0)))) {
            triangles[tid]=uvec4(0u,0u,0u,2u); return;
        }
    }
    precise double p=(b.x-a.x)*(c.y-a.y);
    precise double q=(b.y-a.y)*(c.x-a.x);
    precise double area=p-q;
    if (area==0.0) { triangles[tid]=uvec4(0u); return; }
    if (u_cull_enabled==0u) {
        if (area<0.0) ids=ids.xzy;
    } else if (u_cull_front!=0u) {
        if (area>0.0) { triangles[tid]=uvec4(0u); return; }
        ids=ids.xzy;
    } else if (area<0.0) { triangles[tid]=uvec4(0u); return; }
    triangles[tid]=uvec4(ids,1u);
}
)GLSL";
} // namespace xlle::vsh_shadow_probe
