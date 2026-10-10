"""Authored full clipping, storage, attribute and visible fallback controls."""
from pathlib import Path
from fractions import Fraction
import copy
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_frame_inputs import require
from test_native_scene_clamp_reference import empty_fixture,texture
from native_scene_clamp_reference import clamp_vertices
import native_scene_positive_xy_empty_reference as model

rows=[]

def fixture():
    data=empty_fixture()
    # Finite positive W and fractional depth, with two XY intersections before
    # complete rejection. This is authored, not an observed original output.
    data[2][0][2]=3.5
    data[2][1][3]=2.
    data[2][2][3]=3.
    return data


def execute(data):
    capture,scene,vertices,fog,targets,before,methods=data
    with patch.object(model,'expected_delivery',return_value=(methods,{},{})),\
         patch('native_scene_clamp_reference.expected_delivery',return_value=(methods,{},{})),\
         patch('native_scene_producer_reference.expected_delivery',return_value=(methods,{},{})):
        return model.expected_positive_w_xy_empty_draw(capture,scene,vertices,fog,targets,before,'fma-b-first','fma')


def reject(name,mutate):
    data=fixture();mutate(data);frozen=repr(data)
    try:
        execute(data)
    except (ValueError,AssertionError,IndexError,KeyError) as error:
        require(repr(data)==frozen,'Rejected input mutated')
        rows.append(dict(control=name,passed=True,rejection=type(error).__name__,detail=str(error)))
    else:
        raise AssertionError('Falsely admitted '+name)


class PositiveXYEmptyControls(unittest.TestCase):
    def test_authored_empty_and_visible_contracts(self):
        global rows
        rows=[]
        data=fixture();frozen=copy.deepcopy(data)
        with patch.object(model,'validate_empty_inputs',wraps=model.validate_empty_inputs) as validated,\
             patch.object(model,'previous_expected_draw',side_effect=AssertionError('proved empty fell back')),\
             patch('native_scene_clamp_reference.expected_draw',side_effect=AssertionError('empty raster called')),\
             patch('native_scene_clamp_reference.triangle_colors',side_effect=AssertionError('empty sample called')),\
             patch('native_scene_clamp_reference.precision_domain',side_effect=AssertionError('unused strict precision called')):
            result,changed,stats=execute(data)
        require(result==data[5] and result is not data[5] and not changed and data==frozen,
                'Empty result/storage/input ownership changed')
        require(stats['homogeneous_plane_intersections']>0 and stats['expanded_triangles']==0 and
                stats['positive_w_xy_all_primitives_empty'] and stats['original_fractional_depth_vertices']==1,
                'Complete finite original clip proof absent')
        require(stats['input_prequantization_codec_error_codes']==['0','64/63','0'] and
                stats['codec_precision_unconsumed_because_no_raster_leaves'] and stats['rgb_channel_bound']==1,
                'Unconsumed palette diagnostic or existing bound changed')
        require(validated.call_count==1,'Existing full input/storage validation absent')
        proxy=validated.call_args.args[2]
        require(all(v[:4]==[0.,0.,1.,1.] for v in proxy) and
                all(v[4:]==c[4:] for v,c in zip(proxy,clamp_vertices(data[2]))),'Proxy changed attributes')
        rows.append(dict(control='fractional-Z-positive-W-intersected-XY-empty-preserves-all-roots',passed=True))

        for target_id in (1,2,3):
            reject(f'missing-owned-root-{target_id}',lambda d,n=target_id:d[5].pop(n))
            reject(f'short-owned-root-{target_id}',lambda d,n=target_id:d[5].__setitem__(n,d[5][n][:-1]))
        reject('extra-unowned-root',lambda d:d[5].__setitem__(4,b'extra'))
        for name,mutate in (
            ('capture-count',lambda d:d[0]['header'].__setitem__(4,4)),
            ('scene-count',lambda d:d[1]['header'].__setitem__(5,4)),
            ('fog-count',lambda d:d[3].pop()),
            ('nonfinite-fog',lambda d:d[3].__setitem__(0,float('nan'))),
            ('wrong-active-role',lambda d:d[4][1].__setitem__('role',1)),
            ('repeated-target',lambda d:d[1]['frame_resource_plan'].__setitem__('target_ids',[1,1])),
            ('wrong-shape',lambda d:d[1]['before'][1]['shape'].__setitem__(0,15)),
            ('extra-texture',lambda d:d[1]['textures'].__setitem__(1,d[1]['textures'][0])),
            ('wrong-scene-flag',lambda d:d[1]['header'].__setitem__(8,1)),
            ('wrong-primitive',lambda d:d[0]['header'].__setitem__(2,5)),
            ('unbound-cpu-texture',lambda d:d[1]['textures'][0].__setitem__('cpu',b'changed')),
            ('short-texture',lambda d:(d[1]['textures'][0]['mips'][0].__setitem__('data',b'1234567'),
                                     d[1]['textures'][0].__setitem__('cpu',b'1234567'))),
            ('transparent-texture',lambda d:d[1]['textures'].__setitem__(0,texture(0,0xffff,3))),
            ('wrong-texture-format',lambda d:d[1]['textures'][0]['resource'].__setitem__(3,0)),
            ('unsaturated-alpha',lambda d:d[2][0].__setitem__(7,.5)),
            ('secondary-profile',lambda d:d[2][0].__setitem__(8,.125)),
            ('texture-Q',lambda d:d[2][0].__setitem__(15,2)),
            ('near-W',lambda d:d[2][0].__setitem__(3,.0005))):
            reject(name,mutate)
        for lane in (0,1,2,3,4,12,27):
            reject(f'nonfinite-original-lane-{lane}',lambda d,c=lane:d[2][0].__setitem__(c,float('nan')))
        for method in (0x208,0x308,0x354,0x38c,0x1b08,0x200,0x2c0,0x9c0,0xac0,0x300):
            reject(f'changed-profile-{method:x}',lambda d,m=method:d[6].__setitem__(m,d[6][m]^1))
        for width in (True,-1,512):
            reject(f'invalid-line-width-{width}',lambda d,w=width:d[6].__setitem__(0x380,w))

        with patch.object(model,'positive_w_xy_empty_proof',side_effect=ValueError('unproved actual geometry')),\
             patch.object(model,'validate_empty_inputs',side_effect=AssertionError('proxy before proof')):
            try:
                execute(fixture())
            except ValueError as error:
                require(str(error)=='unproved actual geometry','Wrong proof order')
            else:
                raise AssertionError('Original clip proof bypassed')
        rows.append(dict(control='actual-proof-precedes-validation-proxy',passed=True))

        def visible(data):
            data[6][0x39c]=0x404
            for vertex,xy,z in zip(data[2],((1,1),(1,3),(5,1)),(3.,7.,3.)):
                vertex[:4]=[*map(float,xy),z,1.]

        reject('visible-full-diffuse64-over63-retains-old-precision-failure',visible)
        data=fixture();visible(data)
        data[1]['textures'][0]=texture(0xffff,0,0)
        for vertex in data[2]:vertex[4:7]=[.25]*3
        frozen=copy.deepcopy(data)
        with patch.object(model,'previous_expected_draw',wraps=model.previous_expected_draw) as previous:
            result,changed,stats=execute(data)
        require(previous.call_count==1 and changed and not stats.get('positive_w_xy_all_primitives_empty',False),
                'Visible draw bypassed unchanged raster fallback')
        # Independent half-plane geometry gives the following four samples; white
        # texel * .25 packs to64, and z=3+2*(y-1) gives exact depths4/6.
        # Alpha0x7a and stencil0x5b remain exact.
        require(changed=={17,18,19,33},'Changed authored visible footprint')
        for index in changed:
            require(struct.unpack_from('<I',result[1],index*4)[0]==0x7a404040 and
                    struct.unpack_from('<I',result[2],index*4)[0]==((6 if index==33 else 4)<<8)|0x5b,
                    'Changed independently authored visible color/depth word')
        require(data==frozen,'Visible inputs mutated')
        rows.append(dict(control='visible-white-quarter-diffuse-exact-four-sample-fallback',passed=True))
        self.assertEqual(len(rows),49)
        self.assertTrue(all(row['passed'] for row in rows))


if __name__=='__main__':unittest.main()
