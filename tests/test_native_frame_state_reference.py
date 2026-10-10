"""Authored storage/observation separation and original-replay admission checks."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_frame_inputs import Reader
from native_frame_state_reference import Inputs,selected
from native_scene_draw_reference import replay,target_bytes
from test_native_frame_inputs import authored,words


class FrameStateInputs(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.path=Path(self.temporary.name)/'frame-inputs.bin'
        self.header,self.records,self.rows=authored()
        # Turn the authored GPU aliases into regular owned textures. CPU
        # storage deliberately differs from the supplied sampled GPU bytes.
        roots=[struct.unpack('<15I',data) for kind,data in self.records if kind==2]
        initial=roots[2][9]
        for kind,data in self.records:
            if kind==1 and len(data)==40+0x114:
                descriptor=[0,2,0,0,2]
                for slot in range(16):
                    descriptor.extend([0,16 if slot==9 else 0,0x42 if slot==0 else 0x22 if slot==9 else 2,0])
                data[40:]=words(descriptor);data[8:40]=hashlib.sha256(data[40:]).digest()
            if kind!=1 or len(data)!=40+27*4:continue
            w=list(struct.unpack('<27I',data[40:]));w[1:7]=[7,0,0,0,0,0]
            w[13]=initial;w[15]=7;w[26]=3
            data[40:]=words(w);data[8:40]=hashlib.sha256(data[40:]).digest()

    def reader(self):
        self.path.write_bytes(words(self.header)+b''.join(words([kind,len(data)])+data for kind,data in self.records))
        return Reader(self.path).read()

    def test_four_original_routes_keep_native_outputs_absent(self):
        inputs=Inputs(self.reader())
        for n,kind in enumerate(range(4),1):
            with self.subTest(kind=kind):
                capture,scene=inputs.draw(n)
                self.assertEqual(capture['submission_kind'],kind)
                self.assertIsNone(capture['outputs']);self.assertIsNone(scene['after'])
                self.assertTrue(scene['input_only'])
                self.assertEqual(scene['header'][3:6],capture['header'][2:5])
                self.assertEqual(capture['up_stride'],capture['metadata'][0][0] if kind&1 else 0)

    def test_cpu_storage_and_sampled_gpu_input_remain_distinct(self):
        _,scene=Inputs(self.reader()).draw(1);texture=scene['textures'][0]
        self.assertEqual(texture['cpu'],bytes(range(32,48)))
        self.assertEqual(texture['mips'][0]['data'],bytes(range(16)))
        self.assertNotEqual(texture['cpu'],texture['mips'][0]['data'])

    def test_later_observations_cannot_replace_initial_target(self):
        reader=self.reader();inputs=Inputs(reader);_,scene=inputs.draw(4)
        self.assertEqual(scene['before'][0]['data'],bytes(range(32,48)))
        # The final observation has different bytes and is never a scene input.
        final=[struct.unpack('<3I',data) for kind,data in self.records if kind==3][-2]
        self.assertEqual(reader.asset_bytes(final[2]),bytes(range(48,64)))

    def test_target_roles_keep_their_own_dimensions(self):
        reader=self.reader();reader.targets[2]['width']=4
        _,scene=Inputs(reader).draw(1)
        self.assertEqual(scene['before'][0]['shape'],[2,2,87])
        self.assertEqual(scene['before'][1]['shape'],[4,2,44])

    def test_changed_live_input_changes_identity_after_digest_refresh(self):
        reader=self.reader();before=Inputs(reader).draw(1)[1]['sha256']
        asset=reader.draws[0]['render_asset'];data=bytearray(reader.small[asset]);data[60*4]^=1
        reader.small[asset]=bytes(data);reader.assets[asset]['sha256']=hashlib.sha256(data).hexdigest()
        capture,scene=Inputs(reader).draw(1)
        self.assertEqual(capture['render'][60],1);self.assertNotEqual(scene['sha256'],before)

    def test_unmodelled_view_is_rejected_even_with_fresh_identity(self):
        reader=self.reader();asset=reader.draws[0]['textures'][0]
        data=bytearray(reader.small[asset]);struct.pack_into('<I',data,16*4,1)
        reader.small[asset]=bytes(data);reader.assets[asset]['sha256']=hashlib.sha256(data).hexdigest()
        with self.assertRaisesRegex(ValueError,'alias/view'):Inputs(reader).draw(1)

    def test_profile_selection_is_explicitly_distinct_from_all_draws(self):
        reader=self.reader();args=SimpleNamespace(draws=None,all_draws=False)
        self.assertEqual(selected(reader,args),[1])
        args.all_draws=True;self.assertEqual(selected(reader,args),[1,2,3,4])
        args.draws=[4,2];self.assertEqual(selected(reader,args),[4,2])

    def test_input_only_pixels_and_retained_targets_stop_before_device_creation(self):
        args=SimpleNamespace(observe_shader_state_only=False,retain_targets=False)
        with patch('native_scene_draw_reference.Reference') as device:
            with self.assertRaisesRegex(AssertionError,'Input-only'):
                replay(args,Path('authored'),capture={},scene={'input_only':True})
            device.assert_not_called()
        args.observe_shader_state_only=True;args.retain_targets=True
        with patch('native_scene_draw_reference.Reference') as device:
            with self.assertRaisesRegex(AssertionError,'continuity'):
                replay(args,Path('authored'),capture={},scene={'input_only':True})
            device.assert_not_called()

    def test_one_missing_in_memory_input_stops_before_device_creation(self):
        args=SimpleNamespace(observe_shader_state_only=True,retain_targets=False)
        with patch('native_scene_draw_reference.Reference') as device:
            with self.assertRaisesRegex(AssertionError,'both'):
                replay(args,Path('authored'),capture={},scene=None)
            device.assert_not_called()

    def test_target_word_conversion_preserves_role_and_channel_meaning(self):
        target={'shape':[1,1,28],'data':bytes.fromhex('11223344')}
        self.assertEqual(target_bytes(target),bytes.fromhex('33221144'))
        target={'shape':[1,1,87],'data':bytes.fromhex('11223344')}
        self.assertEqual(target_bytes(target),bytes.fromhex('11223344'))
        target={'shape':[1,1,44],'data':bytes.fromhex('123456ab')}
        self.assertEqual(target_bytes(target,True),bytes.fromhex('ab123456'))
        target['shape'][2]=87
        with self.assertRaises(AssertionError):target_bytes(target,True)


if __name__=='__main__':unittest.main()
