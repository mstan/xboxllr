"""Profile admission guards for the shared D3D binding adapter."""
import copy
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from native_binding import select_profile
from native_d3d_binding import ADAPTER,bind,validate_profile
from native_d3d_ltcg import ADAPTER as LTCG_ADAPTER
from native_d3d_ltcg import inspect,marshal
from native_d3d_ltcg import bind as bind_ltcg
from native_binding import Binding
from native_display_binding import bind as bind_display


class GraphicsProfileTests(unittest.TestCase):
    def setUp(self):
        self.path=ROOT/'pilots/native-title/profiles/blinx-ntsc-u.json'
        self.title=json.loads(self.path.read_text())
        self.proof=select_profile(self.title['xbe_sha256'],'d3d',adapter=ADAPTER)

    def test_default_exact_image_selects_isolated_graphics_proof(self):
        self.assertIs(validate_profile(self.proof),self.proof)
        self.assertEqual(self.proof['layout']['stateblock_fields']['vertex_object'],0x380)
        self.assertEqual(self.proof['layout']['stateblock_fields']['vertex_shader'],0x384)

    def test_unknown_image_cannot_reuse_known_d3d_profile(self):
        with self.assertRaisesRegex(ValueError,'exact XBE'):
            select_profile('0'*64,'d3d',adapter=ADAPTER)

    def test_incomplete_hash_abi_or_layout_proof_declines(self):
        mutations=[lambda p:p['hashes'].pop('CREATE'),
                   lambda p:p['hashes'].update(CREATE='not-a-sha256'),
                   lambda p:p.update(native_create_behavior_mask=16),
                   lambda p:p.update(native_create_behavior_mask=True),
                   lambda p:p['visibility']['D3DDevice_EndVisibilityTest'].update(stack_bytes=0),
                   lambda p:p['visibility']['D3DDevice_EndVisibilityTest'].update(stack_bytes=True),
                   lambda p:p['layout']['transform_fields'].update(screen_offset=3),
                   lambda p:p['layout']['stateblock_fields'].update(vertex_object=-4),
                   lambda p:p['layout']['stateblock_fields'].update(vertex_object=True),
                   lambda p:p.update(adapter='unreviewed-d3d'),
                   lambda p:p.update(hashes=[]),lambda p:p.update(layout=[])]
        for mutate in mutations:
            proof=copy.deepcopy(self.proof);mutate(proof)
            with self.subTest(proof=proof),self.assertRaises(ValueError):
                validate_profile(proof)

    def test_profile_declines_before_generated_body_inspection(self):
        with patch('native_d3d_binding.Binding') as cls:
            cls.return_value.require_profile.return_value={}
            with self.assertRaisesRegex(ValueError,'graphics profile'):
                bind(Path('unopened.xbe'),Path('unwritten-output'),Path('unopened-symbols.json'))
            cls.return_value.require_profile.assert_called_once_with('d3d',None,adapter=(ADAPTER,LTCG_ADAPTER))
            cls.return_value.digest.assert_not_called()

    def test_original_display_requires_pinned_entry_bodies(self):
        proof=select_profile(self.title['xbe_sha256'],'display',adapter='xdk-original-display-v1')
        first=next(iter(proof['entries'].values()))
        with patch('native_display_binding.Binding') as cls:
            context=cls.return_value;context.image={'sha256':self.title['xbe_sha256']}
            context.require_profile.return_value=proof
            context.functions=[dict(start=hex(first['address']),end=hex(first['end']),size=first['end']-first['address'])]
            context.digest.return_value='0'*64
            with patch.object(Path,'read_text',return_value=json.dumps({'xbe_sha256':self.title['xbe_sha256']})),self.assertRaisesRegex(ValueError,'original display body'):
                bind_display(Path('not-opened.xbe'),Path('not-written'),Path('not-opened.json'))
            context.digest.assert_called_once()


class LTCGOriginalProofTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output=ROOT/'build/dashboard-native-20261019'
        if not (output/'xboxdash.xbe').is_file():
            raise unittest.SkipTest('owned dashboard original fixture is not installed')
        cls.context=Binding(output/'xboxdash.xbe',output,output/'xboxdash_xdk_symbols.json')
        cls.proof=select_profile(cls.context.image['sha256'],'d3d',
            ROOT/'pilots/native-title/profiles/dashboard-5960.json',adapter=LTCG_ADAPTER)
        cls.binding,cls.header,cls.wrapper=inspect(cls.context,cls.proof)

    def test_actual_original_bodies_and_layout_pass_without_stateblock_fabrication(self):
        self.assertEqual(len(self.binding['calls']),101)
        self.assertFalse(self.binding['stateblock'])
        self.assertIn('#define PILOT_D3D_STATEBLOCK_AVAILABLE 0',self.header)
        self.assertNotIn('pilot_d3d_block_pixel_render',self.header)
        self.assertNotIn('PILOT_D3D_SNAPSHOT_LIGHTS',self.header)
        self.assertNotIn('PILOT_D3D_PIXEL_PACKED_CONSTANTS',self.header)
        self.assertNotIn('PILOT_D3D_SNAPSHOT_VERTEX_CONSTANTS',self.header)
        self.assertIn('#define PILOT_D3D_VERTEX_CONSTANTS_GLOBAL 0x156328u',self.header)
        self.assertIn('#define PILOT_D3D_CREATE_BEHAVIOR_MASK 0x40u',self.header)
        self.assertIn('#define PILOT_D3D_CREATE_DRIVER_MASK 0x10u',self.header)
        self.assertIn('#define PILOT_D3D_SURFACE_PROJECTION_VERSION 1u',self.header)
        self.assertIn('#define PILOT_D3D_PRESENTATION_COLORS 0x1a14u',self.header)
        self.assertIn('#define PILOT_D3D_PRESENTATION_DEPTH 0x1a20u',self.header)
        self.assertIn('#define PILOT_D3D_CURRENT_COLOR 0x1a04u',self.header)
        self.assertEqual(self.binding['native_create_behavior_mask'],64)
        self.assertEqual(self.binding['original_create_driver_mask'],16)
        self.assertEqual(self.binding['layout']['device_object'],0x153bd0)
        self.assertEqual(self.binding['layout']['cache_fields']['vertex_object'],0x794)

    def test_register_and_original_stack_contracts_are_distinct(self):
        calls={c['name']:c for c in self.binding['calls']}
        create=calls['Direct3D_CreateDevice']
        self.assertEqual(create['argument_sources'],['0u','1u','0u','g_eax','MEM32(g_esp+4u)','g_ecx'])
        self.assertEqual((create['stack_bytes'],create['original_stack_bytes']),(24,4))
        self.assertEqual(calls['D3DDevice_Reset']['argument_sources'],['g_ebx'])
        self.assertEqual(calls['D3DDevice_Swap']['argument_sources'],['g_eax'])
        self.assertEqual(calls['D3DDevice_Swap']['original_stack_bytes'],0)
        # Original RET28 disproves the discovery database's six-argument claim.
        texture=calls['D3DDevice_CreateTexture2']
        self.assertEqual(texture['source_signature']['stack_bytes'],24)
        self.assertEqual(texture['original_stack_bytes'],28)
        self.assertEqual(texture['argument_sources'],[f'MEM32(g_esp+{i}u)' for i in range(4,32,4)])
        self.assertEqual(calls['D3DDevice_DrawIndexedVertices']['original_stack_bytes'],12)

    def test_absent_service_enums_follow_count_and_have_no_call_record(self):
        name='D3DDevice_CreateStateBlock'
        self.assertNotIn(name,{c['name'] for c in self.binding['calls']})
        self.assertGreater(self.header.index('PILOT_D3D_'+name),self.header.index('PILOT_D3D_COUNT'))
        self.assertIn('pilot_d3d_dispatch_frame(PILOT_D3D_Direct3D_CreateDevice,args,6u,4u)',self.wrapper)

    def test_body_inventory_layout_and_ret_changes_decline_before_emission(self):
        for name,mutate in (
                ('body',lambda p:p['functions'][0]['body'].update(sha256='0'*64)),
                ('inventory',lambda p:p['functions'].pop()),
                ('layout',lambda p:p['layout'].update(device_global=0x15607c)),
                ('behavior',lambda p:p.update(native_create_behavior_mask=16)),
                ('driver mask',lambda p:p.update(original_create_driver_mask=64)),
                ('presentation depth',lambda p:p['layout']['presentation_fields'].update(depth=0x1a18)),
                ('current color',lambda p:p['layout']['presentation_fields'].update(current_color=0x1a14)),
                ('presentation count',lambda p:p['layout']['presentation_fields'].update(count=0x1a0c)),
                ('presentation extent',lambda p:p['layout']['presentation_fields'].update(colors=0xfffffff8)),
                ('ret',lambda p:next(r for r in p['functions'] if r['name']=='D3DDevice_CreateTexture2')['signature'].update(stack_bytes=24))):
            proof=copy.deepcopy(self.proof);mutate(proof)
            with self.subTest(name=name),self.assertRaises(ValueError):
                inspect(self.context,proof)


class GraphicsHeaderMigrationTests(unittest.TestCase):
    def fixture(self,directory,initial=False):
        output=Path(directory);generated=output/'gen';generated.mkdir()
        binding={'layout':{'vertex_constants':0x156328,'device_object':0x153bd0,
                          'presentation_fields':dict(count=0x1a10,colors=0x1a14,depth=0x1a20,current_color=0x1a04,current_depth=0x1a08)},
                 'native_create_behavior_mask':64,'original_create_driver_mask':16}
        previous=copy.deepcopy(binding);previous.pop('native_create_behavior_mask');previous.pop('original_create_driver_mask');previous['layout'].pop('presentation_fields')
        header=('#define PILOT_D3D_CREATE_BEHAVIOR_MASK 0x40u\n'
                '#define PILOT_D3D_CREATE_DRIVER_MASK 0x10u\n'
                '#define PILOT_D3D_SURFACE_PROJECTION_VERSION 1u\n'
                '#define PILOT_D3D_PRESENTATION_COUNT 0x1a10u\n'
                '#define PILOT_D3D_PRESENTATION_COLORS 0x1a14u\n'
                '#define PILOT_D3D_PRESENTATION_DEPTH 0x1a20u\n'
                '#define PILOT_D3D_CURRENT_COLOR 0x1a04u\n'
                '#define PILOT_D3D_CURRENT_DEPTH 0x1a08u\n'
                '#define PILOT_D3D_VERTEX_CONSTANTS_GLOBAL 0x156328u\n'
                'static const uint32_t pilot_d3d_render_defaults[] = {1u};\n')
        prior=('#define PILOT_D3D_CREATE_BEHAVIOR_MASK 0x10u\n'
               '#define PILOT_D3D_VERTEX_CONSTANTS_GLOBAL 0x156328u\n'
               'static const uint32_t pilot_d3d_render_defaults[] = {1u};\n')
        if initial:
            prior=prior.replace('#define PILOT_D3D_CREATE_BEHAVIOR_MASK 0x10u\n','').replace(
                'static const uint32_t pilot_d3d_render_defaults[]',
                '#define PILOT_D3D_SNAPSHOT_VERTEX_CONSTANTS 0x2758u\nstatic const uint32_t pilot_d3d_render_defaults[]')
        wrapper='/* exact original wrapper */\n'
        (generated/'pilot_d3d_bindings.h').write_text(prior)
        (generated/'pilot_d3d_binding.c').write_text(wrapper)
        (output/'d3d-binding.aot-private.json').write_text(json.dumps(previous))
        context=SimpleNamespace(output=output,generated=generated,
            manifest={'files':{},'d3d_binding':previous},manifest_path=output/'generation.aot-private.json')
        return context,binding,header,wrapper

    def test_exact_upgrade_preserves_body_wrapper_and_previous_facts_then_is_idempotent(self):
        for initial in (False,True):
            with self.subTest(initial=initial),tempfile.TemporaryDirectory() as directory:
                context,binding,header,wrapper=self.fixture(directory,initial)
                with patch('native_d3d_ltcg.inspect',return_value=(binding,header,wrapper)):
                    self.assertEqual(bind_ltcg(context,{}),binding)
                    self.assertEqual((context.generated/'pilot_d3d_bindings.h').read_text(),header)
                    self.assertEqual(json.loads((context.output/'d3d-binding.aot-private.json').read_text()),binding)
                    self.assertEqual((context.generated/'pilot_d3d_binding.c').read_text(),wrapper)
                    with patch.object(Path,'write_text',side_effect=AssertionError('unexpected repeated write')):
                        self.assertEqual(bind_ltcg(context,{}),binding)

    def test_changed_header_wrapper_or_record_declines_before_any_write(self):
        for name in ('pilot_d3d_bindings.h','pilot_d3d_binding.c','record'):
            with self.subTest(name=name),tempfile.TemporaryDirectory() as directory:
                context,binding,header,wrapper=self.fixture(directory)
                path=(context.output/'d3d-binding.aot-private.json' if name=='record' else context.generated/name)
                path.write_text('{"unreviewed":true}' if name=='record' else path.read_text()+'/* changed */\n')
                with patch('native_d3d_ltcg.inspect',return_value=(binding,header,wrapper)),\
                     patch.object(Path,'write_text',side_effect=AssertionError('write before rejection')),\
                     self.assertRaises(ValueError):
                    bind_ltcg(context,{})


if __name__=='__main__':
    unittest.main()
