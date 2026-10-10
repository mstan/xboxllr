"""API-derived fixed-function NV097 combiners for the captured operation set.

Numeric sources/fields are public NV097 and Direct3D facts. Original helpers
0x140070/0x140110 establish argument routing and the missing-texture fallback;
neither maintained compiler, LLE renderer nor an observed packet supplies an
expected word. TEMP, factor constants, bump and other operations remain guarded.
"""
from native_frame_inputs import require

OP_SOURCE='https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dtextureop'
COMBINER_SOURCE='https://registry.khronos.org/OpenGL/extensions/NV/NV_register_combiners.txt'


def argument(value,stage,alpha):
    require(type(value) is int and value>=0 and not value&~0x3f,'Unmodelled argument flags')
    source=value&15
    require(source in (0,1,2,4),'Unmodelled factor/temp/constant argument')
    register={0:4,1:4 if stage==0 else 12,2:8+stage,4:5}[source]
    return register|(16 if alpha or value&32 else 0)|(32 if value&16 else 0)


def portion(operation,arg1,arg2,stage,alpha,texture_present):
    require(operation in (2,3,4,7),'Unmodelled fixed-function operation')
    consumed=[arg1] if operation==2 else [arg2] if operation==3 else [arg1,arg2]
    require(all((a&15) in (0,1,2,4) and not a&~0x3f for a in consumed),'Unmodelled consumed argument')
    # The original resolver returns a missing-texture sentinel; its compiler
    # replaces the whole portion with DIFFUSE at stage0 or CURRENT thereafter.
    if not texture_present and any((a&15)==2 for a in consumed):
        return argument(1,stage,alpha)<<24 | 32<<16
    first=argument(arg1,stage,alpha) if operation!=3 else 0
    second=argument(arg2,stage,alpha) if operation!=2 else 0
    if operation==2:return first<<24 | 32<<16
    if operation==3:return 32<<8 | second
    if operation==4:return first<<24 | second<<16
    return first<<24 | 32<<16 | 32<<8 | second  # A*1 + 1*D


def expected_fixed_function(capture,scene):
    require(not scene['header'][8] and scene['pixel'] is None,'Custom shader is not fixed function')
    state=capture['texture'];require(len(state)==128 and len(scene['textures'])==4,'Incomplete stage inputs')
    methods={};active=0
    for stage in range(4):
        ts=state[stage*32:(stage+1)*32]
        if ts[12]==1:break
        require(ts[20]==1 and ts[21]==0,'Unmodelled result register/transform')
        present=scene['textures'][stage] is not None
        for method,alpha,op,a,b in ((0xac0,False,ts[12],ts[14],ts[15]),
                                   (0x260,True,ts[16],ts[18],ts[19])):
            methods[method+stage*4]=portion(op,a,b,stage,alpha,present)
        methods[0x1e40+stage*4]=methods[0xaa0+stage*4]=12<<8
        active+=1
    require(active>0,'Disabled first stage needs a separate default-profile check')
    require(not any(scene['textures'][active:]),'Bound texture after disabled stage needs explicit program admission')
    # The original compiler explicitly clears all unused combiner slots.
    for stage in range(active,8):
        for base in (0xac0,0x260,0x1e40,0xaa0):methods[base+stage*4]=0
    methods[0x1e60]=active
    methods[0x1e70]=sum(1<<(stage*5) for stage,t in enumerate(scene['textures']) if t is not None)
    # Original SetPixelShader(0) restores the fixed-function dependent-input
    # routing. PROJECT2D does not consume these routes, but the setter delivers it.
    methods[0x1e78]=(1<<16)|(2<<20)
    return methods
