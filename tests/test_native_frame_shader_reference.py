"""Authored observations check admission without an original title or GPU."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_frame_shader_reference import qualify, observation_for


def fixture():
    # Published MOV oD0,v0.xyzw; position and other outputs keep defaults.
    values=[(.25,.5,.75,2),(-.5,1,2,4)]
    elements=[[2,0,0] for _ in range(16)];elements[0]=[0x42,0,0]
    metadata=[[0,0] for _ in range(16)];metadata[0]=[16,32]
    streams=[b'' for _ in range(16)];streams[0]=struct.pack('<8f',*sum(values,()))
    capture=dict(header=[0,2,5,0,2,1,0,0],elements=elements,metadata=metadata,
        streams=streams,constants=bytes(3072),code=[0,(1<<21)|0x1b,2<<26,
        (15<<12)|(1<<11)|(3<<3)|1],outputs=None,sha256='authored-input',
        input_sequence=1,submission_kind=0)
    counters=[0]*26;counters[1]=1
    observation=dict(input_sequence=1,vertex_capture_sha256='authored-input',
        complete_draw_executed=True,input_only_capture=True,draw_counters=counters,
        submission='original DrawVertices',
        vertices=[[0,0,0,1]+list(v)+[0,0,0,1]*5 for v in values],fog=[0,0])
    return capture,observation


class FrameShaderAdmission(unittest.TestCase):
    def test_authored_values_qualify_without_pixels(self):
        row=qualify(*fixture())
        self.assertTrue(row['software_shader_qualified'])
        self.assertEqual(row['compared_values'],58)
        self.assertFalse(row['complete_scene_pixel_qualified'])

    def test_native_output_field_cannot_supply_expectations(self):
        capture,observation=fixture();capture['outputs']=[12345]*104
        self.assertTrue(qualify(capture,observation)['software_shader_qualified'])

    def test_changed_live_input_with_fresh_provenance_fails_old_output(self):
        capture,observation=fixture()
        capture['streams'][0]=struct.pack('<f',.375)+capture['streams'][0][4:]
        capture['sha256']=observation['vertex_capture_sha256']='changed-input'
        self.assertFalse(qualify(capture,observation)['software_shader_qualified'])

    def test_changed_output_is_a_failure(self):
        capture,observation=fixture();observation['vertices'][0][4]=.375
        self.assertEqual(qualify(capture,observation)['failure_count'],1)

    def test_missing_nonfinite_or_partial_outputs_reject(self):
        for field in ('vertices','fog','lane','nonfinite'):
            with self.subTest(field=field):
                capture,observation=fixture()
                if field in ('vertices','fog'):observation[field].pop()
                elif field=='lane':observation['vertices'][0].pop()
                else:observation['fog'][0]=float('nan')
                with self.assertRaises(ValueError):qualify(capture,observation)

    def test_absent_draw_guarded_service_and_bad_counter_shape_reject(self):
        for fault in ('no-draw','guard','short','negative','bool'):
            with self.subTest(fault=fault):
                capture,observation=fixture();counts=observation['draw_counters']
                if fault=='no-draw':counts[1]=0
                elif fault=='guard':counts[25]=1
                elif fault=='short':counts.pop()
                elif fault=='negative':counts[0]=-1
                else:counts[1]=True
                with self.assertRaises(ValueError):qualify(capture,observation)

    def test_provenance_completion_and_submission_cannot_be_substituted(self):
        for key,value in (('input_sequence',True),('input_sequence',2),
                ('vertex_capture_sha256','different'),('complete_draw_executed',False),
                ('input_only_capture',False),('submission','original DrawVerticesUP')):
            with self.subTest(key=key,value=value):
                capture,observation=fixture();observation[key]=value
                with self.assertRaises(ValueError):qualify(capture,observation)

    def test_up_requires_actual_up_submission(self):
        capture,observation=fixture();capture['submission_kind']=1
        with self.assertRaises(ValueError):qualify(capture,observation)
        observation['submission']='original DrawVerticesUP'
        self.assertTrue(qualify(capture,observation)['software_shader_qualified'])

    def test_retained_union_requires_present_nonconflicting_observations(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);primary=root/'primary';extra=root/'extra'
            primary.mkdir();extra.mkdir()
            args=SimpleNamespace(diagnostics=primary,additional_diagnostics=[extra])
            with self.assertRaises(ValueError):observation_for(args,1)
            observation=fixture()[1];name='input-1.aot-private.json'
            (extra/name).write_text(json.dumps(observation))
            self.assertEqual(observation_for(args,1),observation)
            (primary/name).write_text(json.dumps(observation))
            self.assertEqual(observation_for(args,1),observation)
            changed=copy.deepcopy(observation);changed['vertices'][0][4]=99
            (extra/name).write_text(json.dumps(changed))
            with self.assertRaises(ValueError):observation_for(args,1)


if __name__=='__main__':unittest.main()
