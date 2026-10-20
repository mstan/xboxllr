"""Profile admission guards for the shared D3D binding adapter."""
import copy
import json
import os
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
from native_d3d_classic import ADAPTER as CLASSIC_ADAPTER
from native_d3d_classic import inspect as inspect_classic,bind as bind_classic
from native_d3d_ltcg import inspect,marshal
from native_d3d_ltcg import bind as bind_ltcg
from native_binding import Binding
from native_display_binding import bind as bind_display


class GraphicsProfileTests(unittest.TestCase):
    def test_classic_create_keeps_present_original_arguments(self):
        params=[dict(type='psh',name=n) for n in ('Adapter','DeviceType','hFocusWindow','BehaviorFlags','pPresentationParameters','ppReturnedDeviceInterface')]
        signature=dict(call='stdcall',params=params,stack_bytes=24)
        row=dict(name='Direct3D_CreateDevice',signature=signature)
        canonical,args,registers=marshal(row,{'Direct3D_CreateDevice':signature})
        self.assertEqual(args,[f'MEM32(g_esp+{i}u)' for i in range(4,28,4)])
        self.assertEqual(registers,{})

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
            cls.return_value.require_profile.assert_called_once_with('d3d',None,adapter=(ADAPTER,LTCG_ADAPTER,CLASSIC_ADAPTER))
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


class ClassicOriginalProofTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixture=os.environ.get('XLLE_TEST_CLASSIC_PROJECT')
        if not fixture:raise unittest.SkipTest('explicit owned classic title fixture is not selected')
        project=Path(fixture);paths=json.loads((project/'local/paths.json').read_text())
        cls.context=Binding(Path(paths['xbe']),Path(paths['generated']),Path(paths['xdk_symbols']))
        cls.proof=json.loads((project/'config/revisions/ntsc-u/d3d-classic.json').read_text())
        cls.binding,cls.header,cls.wrapper=inspect_classic(cls.context,cls.proof)

    def test_owned_classic_layout_and_cpu_cleanup(self):
        self.assertEqual(len(self.binding['calls']),131)
        self.assertEqual(self.binding['render_cache']['count'],146)
        calls={c['name']:c for c in self.binding['calls']}
        self.assertEqual((calls['D3DDevice_GetTexture2']['source_signature']['stack_bytes'],calls['D3DDevice_GetTexture2']['original_stack_bytes']),(4,8))
        self.assertEqual((calls['D3D_GetAdapterModeCount']['source_signature']['stack_bytes'],calls['D3D_GetAdapterModeCount']['original_stack_bytes']),(8,4))
        self.assertIn('PILOT_D3D_GAMMA_CACHE_VERSION',self.header)

    def test_changed_original_abi_layout_or_cache_declines(self):
        changes=[lambda p:p['functions'][0]['body'].update(sha256='0'*64),
                 lambda p:p['functions'][0]['signature'].update(stack_bytes=0),
                 lambda p:p['render_cache']['map'].__setitem__(158,138),
                 lambda p:p['layout'].update(vertex_constants=p['layout']['vertex_constants']+16),
                 lambda p:p['layout']['cache_fields'].update(vertex_shader=0x388),
                 lambda p:p['layout']['gamma_cache'].update(current=p['layout']['gamma_cache']['current']+4),
                 lambda p:p.update(references=[r for r in p['references'] if r.get('field')!='packed_pixel_constants'])]
        for change in changes:
            with self.subTest(change=change):
                proof=copy.deepcopy(self.proof);change(proof)
                with self.assertRaises(ValueError):inspect_classic(self.context,proof)

    def test_additive_gamma_proof_preserves_original_code_and_rejects_drift(self):
        from native_d3d_ltcg import emit_text,INTERFACE
        interface={r['name']:r for r in json.loads(INTERFACE.read_text())}
        previous=copy.deepcopy(self.binding);previous['layout'].pop('gamma_cache');previous['helpers'].pop('gamma_initializer')
        old_header,old_wrapper=emit_text(previous,interface,0x40)
        for drift in (False,True):
            with self.subTest(drift=drift),tempfile.TemporaryDirectory() as directory:
                context=copy.copy(self.context);context.output=Path(directory);context.generated=context.output/'gen';context.generated.mkdir()
                context.manifest={'d3d_binding':previous,'files':{}};context.manifest_path=context.output/'generation.aot-private.json'
                (context.output/'d3d-binding.aot-private.json').write_text(json.dumps(previous))
                (context.generated/'pilot_d3d_bindings.h').write_text(old_header)
                (context.generated/'pilot_d3d_binding.c').write_text(old_wrapper+('/* drift */' if drift else ''))
                if drift:
                    with patch.object(Path,'write_text',side_effect=AssertionError('write before rejection')),self.assertRaises(ValueError):bind_classic(context,self.proof)
                else:
                    self.assertEqual(bind_classic(context,self.proof),self.binding)
                    self.assertEqual((context.generated/'pilot_d3d_binding.c').read_text(),old_wrapper)
                    with patch.object(Path,'write_text',side_effect=AssertionError('repeated write')):self.assertEqual(bind_classic(context,self.proof),self.binding)


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
        self.assertIn('#define PILOT_D3D_GUEST_PRESENTATION_VERSION 1u',self.header)
        self.assertEqual(self.binding['guest_presentation']['sample_types'],[0x11,0x2222])
        self.assertEqual(self.binding['native_create_behavior_mask'],64)
        self.assertEqual(self.binding['original_create_driver_mask'],16)
        self.assertEqual(self.binding['layout']['device_object'],0x153bd0)
        self.assertEqual(self.binding['layout']['cache_fields']['vertex_object'],0x794)

    def test_inline_command_policy_is_proved_from_original_defaults_and_boundaries(self):
        self.assertEqual(self.binding['command_buffer']['request_default'],0x8000)
        self.assertEqual(self.binding['command_buffer']['capacity_default'],0x80000)
        for key,value in [('margin',512),('current_offset',4),('limit_offset',8),
                          ('request_global',0x157238),('request_default',0x4000),
                          ('capacity_default',0x40000),('reserve','D3DDevice_MakeSpace'),('record_mask',8)]:
            proof=copy.deepcopy(self.proof);proof['command_buffer'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):inspect(self.context,proof)

    def test_index_data_publication_is_proved_from_original_setter(self):
        self.assertEqual(self.binding['layout']['index_data_global'],0x156f28)
        self.assertIn('#define PILOT_D3D_INDEX_DATA_GLOBAL 0x156f28u',self.header)
        proof=copy.deepcopy(self.proof);proof['layout']['index_data_global']+=4
        with self.assertRaisesRegex(ValueError,'layout/reference'):
            inspect(self.context,proof)

    def test_device_reference_field_is_proved_from_both_original_methods(self):
        self.assertEqual(self.binding['layout']['device_references'],0x938)
        self.assertIn('#define PILOT_D3D_DEVICE_REFERENCES 0x938u',self.header)
        proof=copy.deepcopy(self.proof);proof['layout']['device_references']+=4
        with self.assertRaisesRegex(ValueError,'layout/reference'):
            inspect(self.context,proof)

    def test_projection_viewport_destination_is_distinct_and_source_proved(self):
        self.assertEqual(self.binding['layout']['cache_fields']['transforms'],0xc60)
        self.assertEqual(self.binding['layout']['transform_fields']['projection_viewport'],0x980)
        for offset in (0xca0,0xc60,0x984,0x97c):
            with self.subTest(offset=offset):
                proof=copy.deepcopy(self.proof);proof['layout']['transform_fields']['projection_viewport']=offset
                with self.assertRaises(ValueError):inspect(self.context,proof)

    def test_projection_alias_correction_preserves_sources_and_rejects_drift(self):
        for drift in (None,'header','record','wrapper'):
            with self.subTest(drift=drift),tempfile.TemporaryDirectory() as directory:
                output=Path(directory);generated=output/'gen';generated.mkdir()
                previous=copy.deepcopy(self.binding);previous['layout']['transform_fields']['projection_viewport']=0xca0
                header=self.header.replace('PILOT_D3D_TRANSFORM_PROJECTION_VIEWPORT 0x980u','PILOT_D3D_TRANSFORM_PROJECTION_VIEWPORT 0xca0u')
                if drift=='header':header+='/* drift */\n'
                if drift=='record':previous['device_object']+=4
                wrapper=self.wrapper+('/* drift */\n' if drift=='wrapper' else '')
                (generated/'pilot_d3d_bindings.h').write_text(header)
                (generated/'pilot_d3d_binding.c').write_text(wrapper)
                record=output/'d3d-binding.aot-private.json';record.write_text(json.dumps(previous))
                context=SimpleNamespace(output=output,generated=generated,manifest={'files':{},'d3d_binding':previous},
                                        manifest_path=output/'generation.aot-private.json')
                before={p.name:p.read_bytes() for p in generated.iterdir()}
                with patch('native_d3d_ltcg.inspect',return_value=(self.binding,self.header,self.wrapper)):
                    if drift:
                        with self.assertRaises(ValueError):bind_ltcg(context,self.proof)
                        self.assertEqual(before,{p.name:p.read_bytes() for p in generated.iterdir()})
                    else:
                        bind_ltcg(context,self.proof)
                        self.assertEqual((generated/'pilot_d3d_binding.c').read_text(),self.wrapper)
                        self.assertEqual((generated/'pilot_d3d_bindings.h').read_text(),self.header)
                        with patch.object(Path,'write_text',side_effect=AssertionError('repeated write')):
                            self.assertEqual(bind_ltcg(context,self.proof),self.binding)

    def test_exact_index_metadata_upgrade_retains_existing_source_and_guards_drift(self):
        for drift in (None,'header','record','wrapper'):
            with self.subTest(drift=drift),tempfile.TemporaryDirectory() as directory:
                output=Path(directory);generated=output/'gen';generated.mkdir()
                previous=copy.deepcopy(self.binding);previous['layout'].pop('index_data_global')
                header='\n'.join(line for line in self.header.splitlines()
                                 if not line.startswith('#define PILOT_D3D_INDEX_DATA_GLOBAL '))+'\n'
                if drift=='header':header+='/* drift */\n'
                if drift=='record':previous['device_object']+=4
                wrapper=self.wrapper+('/* drift */\n' if drift=='wrapper' else '')
                (generated/'pilot_d3d_bindings.h').write_text(header)
                (generated/'pilot_d3d_binding.c').write_text(wrapper)
                record=output/'d3d-binding.aot-private.json';record.write_text(json.dumps(previous))
                context=SimpleNamespace(output=output,generated=generated,manifest={'files':{},'d3d_binding':previous},
                                        manifest_path=output/'generation.aot-private.json')
                before={p.name:p.read_bytes() for p in generated.iterdir()}
                with patch('native_d3d_ltcg.inspect',return_value=(self.binding,self.header,self.wrapper)):
                    if drift:
                        with self.assertRaises(ValueError):bind_ltcg(context,self.proof)
                        self.assertEqual(before,{p.name:p.read_bytes() for p in generated.iterdir()})
                    else:
                        bind_ltcg(context,self.proof)
                        self.assertEqual((generated/'pilot_d3d_bindings.h').read_text(),self.header)
                        self.assertEqual((generated/'pilot_d3d_binding.c').read_text(),self.wrapper)
                        self.assertEqual(json.loads(record.read_text()),self.binding)
                        bind_ltcg(context,self.proof)

    def test_exact_command_metadata_upgrade_preserves_wrappers_and_is_idempotent(self):
        for drift in (None,'header','record','wrapper','remove'):
            with self.subTest(drift=drift),tempfile.TemporaryDirectory() as directory:
                output=Path(directory);generated=output/'gen';generated.mkdir()
                previous=copy.deepcopy(self.binding);previous.pop('command_buffer')
                header='\n'.join(line for line in self.header.splitlines() if not line.startswith('#define PILOT_D3D_COMMANDS_'))+'\n'
                if drift=='header':header+='/* unreviewed */\n'
                if drift=='record':previous['device_object']+=4
                wrapper=self.wrapper+('/* unreviewed */\n' if drift=='wrapper' else '')
                selected=previous if drift=='remove' else self.binding
                recorded=self.binding if drift=='remove' else previous
                (generated/'pilot_d3d_bindings.h').write_text(self.header if drift=='remove' else header)
                (generated/'pilot_d3d_binding.c').write_text(wrapper)
                (output/'d3d-binding.aot-private.json').write_text(json.dumps(recorded))
                context=SimpleNamespace(output=output,generated=generated,manifest={'files':{},'d3d_binding':recorded},
                                        manifest_path=output/'generation.aot-private.json')
                before={p.name:p.read_bytes() for p in generated.iterdir()}
                with patch('native_d3d_ltcg.inspect',return_value=(selected,self.header,self.wrapper)):
                    if drift:
                        with self.assertRaises(ValueError):bind_ltcg(context,{})
                        self.assertEqual(before,{p.name:p.read_bytes() for p in generated.iterdir()})
                    else:
                        self.assertEqual(bind_ltcg(context,{}),self.binding)
                        self.assertEqual((generated/'pilot_d3d_binding.c').read_text(),wrapper)
                        with patch.object(Path,'write_text',side_effect=AssertionError('repeated write')):
                            self.assertEqual(bind_ltcg(context,{}),self.binding)
    def test_original_allocator_helpers_are_source_proved_and_complete(self):
        self.assertEqual(self.binding['resource_retirement'],dict(version=1,unregister=0x1484c0,tagged_free=0x6fdf9))
        self.assertIn('#define PILOT_D3D_RESOURCE_RETIRE_VERSION 1u',self.header)
        for helper in ('resource_unregister','resource_free'):
            for mutation in ('missing','body','address'):
                with self.subTest(helper=helper,mutation=mutation):
                    proof=copy.deepcopy(self.proof)
                    if mutation=='missing':proof['helpers'].pop(helper)
                    elif mutation=='body':proof['helpers'][helper]['sha256']='0'*64
                    else:proof['helpers'][helper]['address']+=4
                    with self.assertRaises(ValueError):inspect(self.context,proof)

    def test_exact_allocator_metadata_upgrade_preserves_cpu_sources_and_rejects_drift(self):
        prefixes=('#define PILOT_D3D_RESOURCE_RETIRE_VERSION ',
                  '#define PILOT_D3D_RESOURCE_UNREGISTER ','#define PILOT_D3D_RESOURCE_TAGGED_FREE ')
        for drift in (None,'header','record','wrapper'):
            with self.subTest(drift=drift),tempfile.TemporaryDirectory() as directory:
                output=Path(directory);generated=output/'gen';generated.mkdir()
                previous=copy.deepcopy(self.binding);previous.pop('resource_retirement')
                for helper in ('resource_unregister','resource_free'):previous['helpers'].pop(helper)
                header='\n'.join(line for line in self.header.splitlines() if not line.startswith(prefixes))+'\n'
                if drift=='header':header+='/* drift */\n'
                if drift=='record':previous['device_object']+=4
                wrapper=self.wrapper+('/* drift */\n' if drift=='wrapper' else '')
                (generated/'pilot_d3d_bindings.h').write_text(header)
                (generated/'pilot_d3d_binding.c').write_text(wrapper)
                record=output/'d3d-binding.aot-private.json';record.write_text(json.dumps(previous))
                context=SimpleNamespace(output=output,generated=generated,manifest={'files':{},'d3d_binding':previous},
                                        manifest_path=output/'generation.aot-private.json')
                before={p.name:p.read_bytes() for p in generated.iterdir()}
                with patch('native_d3d_ltcg.inspect',return_value=(self.binding,self.header,self.wrapper)):
                    if drift:
                        with self.assertRaises(ValueError):bind_ltcg(context,self.proof)
                        self.assertEqual(before,{p.name:p.read_bytes() for p in generated.iterdir()})
                    else:
                        bind_ltcg(context,self.proof)
                        self.assertEqual((generated/'pilot_d3d_binding.c').read_text(),self.wrapper)
                        self.assertEqual((generated/'pilot_d3d_bindings.h').read_text(),self.header)
                        self.assertEqual(json.loads(record.read_text()),self.binding)
                        with patch.object(Path,'write_text',side_effect=AssertionError('repeated write')):
                            self.assertEqual(bind_ltcg(context,self.proof),self.binding)

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
                ('sample mode',lambda p:p['guest_presentation'].update(sample_types=[0x11,0x1222])),
                ('sample flag type',lambda p:p['guest_presentation'].update(version=True)),
                ('filter role',lambda p:p['guest_presentation'].update(filter_global=p['guest_presentation']['mode_global'])),
                ('AA cache role',lambda p:p['guest_presentation'].update(aa_state=153)),
                ('resolve body',lambda p:p['helpers']['resolve_setup'].update(sha256='0'*64)),
                ('ret',lambda p:next(r for r in p['functions'] if r['name']=='D3DDevice_CreateTexture2')['signature'].update(stack_bytes=24))):
            proof=copy.deepcopy(self.proof);mutate(proof)
            with self.subTest(name=name),self.assertRaises(ValueError):
                inspect(self.context,proof)


class GraphicsHeaderMigrationTests(unittest.TestCase):
    def test_exact_presentation_upgrade_from_projected_record_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);generated=output/'gen';generated.mkdir()
            previous={'layout':{},'helpers':{'retained':{'sha256':'1'*64}}}
            binding=copy.deepcopy(previous);binding['guest_presentation']={'version':1}
            binding['helpers']['resolve_setup']={'sha256':'2'*64}
            wrapper='/* retained wrapper and original bodies */\n'
            old='#pragma once\n#define PILOT_D3D_SURFACE_PROJECTION_VERSION 1u\n'
            header=old+'#define PILOT_D3D_GUEST_PRESENTATION_VERSION 1u\n#define PILOT_D3D_SAMPLE_AA_STATE 152u\n#define PILOT_D3D_SAMPLE_MASK_STATE 153u\n'
            (generated/'pilot_d3d_bindings.h').write_text(old)
            (generated/'pilot_d3d_binding.c').write_text(wrapper)
            (output/'d3d-binding.aot-private.json').write_text(json.dumps(previous))
            context=SimpleNamespace(output=output,generated=generated,manifest={'files':{},'d3d_binding':previous},
                                    manifest_path=output/'generation.aot-private.json')
            with patch('native_d3d_ltcg.inspect',return_value=(binding,header,wrapper)):
                self.assertEqual(bind_ltcg(context,{}),binding)
                self.assertEqual((generated/'pilot_d3d_bindings.h').read_text(),header)
                self.assertEqual((generated/'pilot_d3d_binding.c').read_text(),wrapper)
                with patch.object(Path,'write_text',side_effect=AssertionError('repeat mutation')):
                    self.assertEqual(bind_ltcg(context,{}),binding)

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
