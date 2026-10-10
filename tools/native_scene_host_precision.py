#!/usr/bin/env python3
"""Independent D3D11 finite shader intervals, derived before GPU observations.

The retained Kelvin instruction decoder belongs to the independent oracle, not
the maintained runtime. Each arithmetic interval comes from D3D11.3 sections
3.1, 3.2.3, 22.10.18 and 22.10.19. No observed maximum supplies an allowance.
Only this fixture's finite, non-indexed opcode/fetch profile is admitted.
"""
from fractions import Fraction
from itertools import permutations
import math
import struct

from native_scene_shader_reference import evaluate, sqrt32

SPEC='https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm'
EPS=Fraction(1,2**21)  # published shader rcp/rsq maximum relative error


class Interval:
    def __init__(self,lo,hi=None):
        self.lo=Fraction(lo);self.hi=Fraction(lo if hi is None else hi)
        assert self.lo<=self.hi
    def __neg__(self):return Interval(-self.hi,-self.lo)
    def __rmul__(self,value):
        assert value in (-1,1)
        return self if value==1 else -self
    def contains(self,value):return self.lo<=value<=self.hi


def exact(value):return value if isinstance(value,Interval) else Interval(value)


def ulp(value):
    value=abs(Fraction(value))
    if not value:return Fraction(1,2**149)
    exponent=value.numerator.bit_length()-value.denominator.bit_length()
    if value<Fraction(2)**exponent:exponent-=1
    assert -126<=exponent<=127,'Subnormal/overflow math needs a separate contract'
    return Fraction(2)**(exponent-23)


def rounded(value):
    value=exact(value)
    if value.lo==value.hi==0:return value
    radius=max(ulp(value.lo),ulp(value.hi))/2
    # Outward rational endpoints also include fused implementations more
    # accurate than the worst serial implementation allowed by section 3.1.
    return Interval(value.lo-radius,value.hi+radius)


def add(a,b):
    a,b=exact(a),exact(b)
    return rounded(Interval(a.lo+b.lo,a.hi+b.hi))


def multiply(a,b):
    a,b=exact(a),exact(b)
    if a.lo==a.hi==0 or b.lo==b.hi==0:return Interval(0)
    values=[x*y for x in (a.lo,a.hi) for y in (b.lo,b.hi)]
    return rounded(Interval(min(values),max(values)))


def clamp(value,lo,hi):
    value=exact(value)
    return Interval(max(lo,min(hi,value.lo)),max(lo,min(hi,value.hi)))


def reciprocal(value):
    value=exact(value);assert value.lo*value.hi>0,'Reciprocal interval crosses zero'
    lo,hi=sorted((1/value.lo,1/value.hi));radius=max(abs(lo),abs(hi))*EPS
    return Interval(lo-radius,hi+radius)


def reciprocal_root(value):
    value=exact(value)
    if value.hi<0:value=-value
    assert value.lo>0,'Reciprocal-root interval crosses zero'
    # sqrt32's exact midpoint-square proof supplies bounds for the irrational
    # root, rather than trusting a host double sqrt as the expectation.
    lower=sqrt32(value.lo);upper=sqrt32(value.hi)
    root_lo=lower-ulp(lower)/2;root_hi=upper+ulp(upper)/2
    lo,hi=1/root_hi,1/root_lo;radius=hi*EPS
    return Interval(lo-radius,hi+radius)


class HostArithmetic:
    exact=staticmethod(exact)
    @staticmethod
    def inputs(capture,inputs):
        rows=[[exact(v) for v in row] for row in inputs]
        for slot,(fmt,stream,offset) in enumerate(capture['elements']):
            if fmt!=0x16:continue
            # Packed 11/11/10 is unpacked with exact integer shifts, then a
            # HLSL divide by 1023/511. A compiler may use rcp+mul, so apply
            # the published rcp error before the multiplication rounding.
            for lane in range(3):
                value=inputs[slot][lane]
                radius=abs(value)*EPS
                rows[slot][lane]=clamp(rounded(Interval(value-radius,value+radius)),-1,1)
        return rows
    @staticmethod
    def vector(op,a,b,c):
        if not op:return None
        if op==1:return a
        if op==3:return [add(x,y) for x,y in zip(a,c)]
        if op in (9,10):
            fn=min if op==9 else max
            return [Interval(fn(exact(x).lo,exact(y).lo),fn(exact(x).hi,exact(y).hi)) for x,y in zip(a,b)]
        assert op in (2,4,5,6,7),op
        products=[multiply(x,y) for x,y in zip(a,b)]
        if op==2:return products
        if op==4:return [add(p,z) for p,z in zip(products,c)]
        terms=products[:3] if op in (5,6) else products
        if op==6:terms.append(exact(b[3]))
        # D3D's fused dot allowance is relative to the worst serial ordering.
        sums=[]
        for ordering in permutations(terms):
            result=ordering[0]
            for term in ordering[1:]:result=add(result,term)
            sums.append(result)
        value=Interval(min(s.lo for s in sums),max(s.hi for s in sums))
        return [value]*4
    @staticmethod
    def scalar(op,c):
        if not op:return None
        if op==1:return c
        assert op in (2,3,4)
        value=reciprocal_root(c[0]) if op==4 else reciprocal(c[0])
        if op==3:
            lo,hi=Fraction(1,2**64),Fraction(2**64)
            value=clamp(value,lo,hi) if value.lo>0 else clamp(value,-hi,-lo)
        return [value]*4


def expectations(capture,raster=False):
    result=[]
    scale=[Fraction(struct.unpack('<f',struct.pack('<f',v))[0]) for v in
        (2/capture['viewport'][2],-2/capture['viewport'][3],1/16777215)]
    for vertex in range(capture['header'][4]):
        raw,masks=evaluate(capture,vertex,HostArithmetic())
        raw=[[exact(v) for v in row] for row in raw]
        if raster:
            pos=raw[0][:]
            raw[0]=[multiply(add(multiply(pos[lane],scale[lane]),(-1,1,0)[lane]),pos[3])
                for lane in range(3)]+[pos[3]]
            raw[3]=[clamp(v,0,1) for v in raw[3]]
            raw[4]=[clamp(v,0,1) for v in raw[4]]
            # The admitted captured state is EXP fog with zero density.
            assert capture['render'][92]==1 and capture['render'][93]==1 and capture['render'][97]==0
            raw[5][0]=Interval(1)
        result.append(raw)
    return result


def qualify(capture,data,raster=False):
    expected=expectations(capture,raster)  # compute before reading observation
    assert len(data)==len(expected)*52*4,'Missing/extra shader observations'
    actual=list(struct.iter_unpack('<52f',data));failures=[];compared=0;maximum_radius=0
    widths=[4,0,0,4,4,1,1,4,4,4,4,4,4]
    for vertex,(row,want) in enumerate(zip(actual,expected)):
        for reg,width in enumerate(widths):
            for lane in range(width):
                got=row[reg*4+lane];interval=want[reg][lane]
                if not math.isfinite(got) or not interval.contains(Fraction(got)):
                    failures.append([vertex,reg,lane,got,float(interval.lo),float(interval.hi)])
                maximum_radius=max(maximum_radius,float((interval.hi-interval.lo)/2));compared+=1
    return dict(shader_qualified=not failures,compared_lanes=compared,failure_count=len(failures),
        first_failures=failures[:16],maximum_derived_interval_radius=maximum_radius,
        scope='Production raster_main bytecode' if raster else 'Raw main diagnostic bytecode',
        bound_source=SPEC),actual
