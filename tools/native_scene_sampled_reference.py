"""Input-bound sampled BC1 precision for a reviewed original producer draw.

A pre-observation source/runtime proof replaces only the coarse palette bound.
Nominal clip, coverage, depth, RGB and storage equations remain unchanged.
Replay requires the matching original inputs and independently expected roots,
source/DLL generation, actual loaded CRT and applicable AMD accuracy premise.
The retained full-frame state report keeps its original generation identity;
every new prefix row must establish current original state and pixel behavior.
"""
from fractions import Fraction
from pathlib import Path
import copy
import hashlib
import json
import math
import struct
import sys

import numpy as np
sys.path.insert(0,'tools')
from native_frame_inputs import require
from native_frame_state_delivery import expected_delivery
from native_scene_diffuse_reference import consumed_profile
from native_scene_pixel_reference import bc1_images
from native_scene_clamp_reference import (
    clamp_vertices,clipped_triangles,precision_domain,validate_empty_inputs,
    expected_draw,triangle_colors,CLAMP_ORDER)
from native_scene_surviving_mixed_reference import expected_bounded_surviving_draw,surviving_software_evidence
from native_scene_rejection_reference import pe_bytes

ROOT=Path('build/blinx/frame-inputs-firstworld-20261010')
SOURCE_SHA='d3ad665e3c03d2d5c60e4bf5b527ef00b0b4d6180a6f1f56a959199052a03c89'
LIBRARY_SHA='bf0c9c51224e9fa593d83dc525bc7c588a9af02738bce35f96749df57b357e19'
COMPLETE_PROOF_SHA='6b2e58505b3faa1f8fd0934e1caa4157aafd15faa318c4d07ad3abb89027f97f'
COMPLETE_HANDOFF_SHA='78c9774939227ea0c91e5d485262580b1042d89b21a590ed4466f47e4af42355'
CRT_SHA='a1bd0acd64e1a60368b8878d3987f603d966ed45be2252d2d4d0f92feb10fa8e'
CLAIM_SHA={
    'capture':'1efe9cf812fe4734693060b9f7bccc1dce5ba29771df709926227f3f63c1dd79',
    'scene':'28e8462fba08fcd43c96df564c021d814998b3f94b09fdd94f59380715d37947',
    'vertices':'42ff128d94b543be2cf495498ea20b805e2ab6cf7bcea76817b73298afa28f30',
    'fog':'40f5607d1768d43b7869b1b3361224f0d0f3f5a0d5ef044a2ebe82de45c250bc',
    'targets':'5d6d41539755c2ae21463d88205447b9b54979b28de1adc2911d83c5f64029ae'}
PREDECESSORS={
    1:'3630e065eb7b4540fbab11dbfd2619e8500f211b9c404380a1867fdc44b77c0c',
    2:'1b63f59ea5652370e3fb9cea6fc084b5c0331c8ffaf49d3c22c79b694a983912',
    3:'7120364207585a78f5d31c262400dfe9d9afa4b4f97b59279f5ea909d19c777b',
    4:'ca605d0d2891cb0e00c8cc2eb5250995df1e17f10ba28544b748d87c262e79d0',
    5:'b79de1efcadcab87b0570069dce2e08cc2acb3be7479d03db30ab09535b22d37'}
sha=lambda data:hashlib.sha256(data).hexdigest()


def normalized(value):
    if isinstance(value,bytes):return {'bytes':len(value),'sha256':sha(value)}
    if isinstance(value,dict):return {str(k):normalized(v) for k,v in value.items()}
    if isinstance(value,(list,tuple)):return [normalized(v) for v in value]
    return value


def fingerprint(value):
    return sha(json.dumps(normalized(value),sort_keys=True,separators=(',',':'),allow_nan=False).encode())


def validate_frozen_proof():
    require(sha((ROOT/'producer2129-sampler-complete-handoff.aot-private.json').read_bytes())==COMPLETE_HANDOFF_SHA,
        'Changed complete pre-observation handoff')
    handoff=json.loads((ROOT/'producer2129-sampler-complete-handoff.aot-private.json').read_text())
    require(all(sha((ROOT/name).read_bytes())==digest for name,digest in handoff['artifact_sha256'].items()),
        'Changed full sampler proof dependency')
    require(sha((ROOT/'producer2129-sampler-complete-proof.aot-private.json').read_bytes())==COMPLETE_PROOF_SHA,
        'Changed source-derived full RGB proof')
    require(sha(Path('tools/native_scene_clamp_reference.py').read_bytes())==
        '81a1796c4b720e4d4c351c33b3d0f5d17ea590a7b355d53c26f30dc3f1cd2d24',
        'Changed nominal oracle source')
    proof=json.loads((ROOT/'producer2129-sampler-complete-proof.aot-private.json').read_text())
    require(all(sha((ROOT/name).read_bytes())==digest for name,digest in proof['bound_artifact_sha256'].items()),
        'Changed transitive sampler bound dependency')
    set_handoff=json.loads((ROOT/'producer2129-sampler808-handoff.aot-private.json').read_text())
    require(all(sha((ROOT/name).read_bytes())==digest for name,digest in set_handoff['artifact_sha256'].items()),
        'Changed transitive sampled-set derivation')
    require(proof['full_rgb_one_code_bound_proved'] and proof['all_controls_passed'] and
        not proof['original_renderer_executed'] and proof['preserved_existing_rgb_tolerance']==1 and
        all(Fraction(v)<1 for v in proof['channel_total_error_codes']),
        'Unproved or widened sampled RGB contract')
    return proof


def validated2129_errors(capture,scene,vertices,fog,targets,before):
    require(capture.get('input_sequence')==2129,'Only original2129 has this sampled-set proof')
    for name,value in [('capture',capture),('scene',scene),('vertices',vertices),('fog',fog),('targets',targets)]:
        require(fingerprint(value)==CLAIM_SHA[name],'Changed bound original2129 '+name)
    require(set(before)==set(PREDECESSORS) and
        all(sha(data)==PREDECESSORS[n] for n,data in before.items()),
        'Wrong independent2128 predecessor storage')
    proof=validate_frozen_proof()
    return list(map(Fraction,proof['exact_codec_errors_codes']))


def verify_for_replay(library,source,captures):
    """Read-only source/input/live-loader binding before any renderer creation.

    Loading the DLL permits a host data read; no exported math/reference or
    renderer function is called. DllMain belongs to the usual loader operation.
    """
    validate_frozen_proof()
    require(sha(Path(library).read_bytes())==LIBRARY_SHA and sha(Path(source).read_bytes())==SOURCE_SHA,
        'Wrong source/DLL generation for sampled precision')
    require(sha((Path(captures)/'frame-inputs.bin').read_bytes())==
        'e8f3fa0c4dd3aaff0efa4f555bcd143a2f0c67255e0e58994ffe9fd6aa119bd9' and
        sha((Path(captures)/'frame.jsonl').read_bytes())==
        '6d600bc6b0f04f04b0950e94541797a549b5d35ef500d6bc592cc99eb7121dce',
        'Wrong current full input frame')
    for n,digest in PREDECESSORS.items():
        require(sha((ROOT/f'positive-w-empty-continuation-preflight-2128-expected-{n}.aot-private.bin').read_bytes())==digest,
            'Changed pre-observation independently expected2128 roots')
    software=surviving_software_evidence(Path(library),Path(source))
    require(software['depth_arithmetic']=='fma-b-first' and software['clip_arithmetic']=='fma' and
        len(software['code_sites'])==65,'Wrong clip/raster/depth generation')
    import ctypes
    from ctypes import wintypes
    import winreg
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as key:
        require(winreg.QueryValueEx(key,'VendorIdentifier')[0]=='AuthenticAMD',
            'Unproved host x87 transcendental premise')
    module=ctypes.WinDLL(str(Path(library).resolve()),use_last_error=True)
    target=ctypes.c_void_p.from_address(module._handle+0x933c8).value
    require(target is not None,'Unresolved hypot IAT')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GetModuleHandleExW.argtypes=[wintypes.DWORD,ctypes.c_void_p,ctypes.POINTER(wintypes.HMODULE)]
    kernel.GetModuleHandleExW.restype=wintypes.BOOL
    handle=wintypes.HMODULE()
    require(kernel.GetModuleHandleExW(0x6,ctypes.c_void_p(target),ctypes.byref(handle)),
        'Cannot identify loaded hypot target module')
    kernel.GetModuleFileNameW.argtypes=[wintypes.HMODULE,wintypes.LPWSTR,wintypes.DWORD]
    kernel.GetModuleFileNameW.restype=wintypes.DWORD
    path=ctypes.create_unicode_buffer(32768)
    require(kernel.GetModuleFileNameW(handle,path,len(path)),'Cannot read loaded CRT module path')
    runtime_path=Path(path.value)
    require(runtime_path.samefile('C:/Windows/System32/msvcrt.dll') and
        sha(runtime_path.read_bytes())==CRT_SHA and target-handle.value==0x6ec60,
        'Changed actual loaded CRT target/module')
    runtime_data=runtime_path.read_bytes()
    checks=[]
    for rva,size,digest in [(0x6ec60,641,'fba7263c10a7c12a28ac2c484f60af2c3ef32af760fff768bac05a6e7e245afb'),
        (0x865e8,8,'dd46fdd197731f40f29d789fd02be525b10ff16ea3b7830c9f2c5b28131420ff')]:
        loaded=ctypes.string_at(handle.value+rva,size)
        require(sha(loaded)==digest and loaded==pe_bytes(runtime_data,0x180000000+rva,size),
            'Changed actual loaded CRT finite code/constant')
        checks.append(dict(rva=hex(rva),bytes=size,sha256=digest,loaded_equals_retained_pe=True))
    # Keep the read-only loader handle alive through replay without any C call.
    return dict(source_sha256=SOURCE_SHA,library_sha256=LIBRARY_SHA,crt_sha256=CRT_SHA,
        checks=checks,complete_rgb_proof_sha256=COMPLETE_PROOF_SHA),module


def validate_source_transition(args,source_report,coverage):
    """Preserve old all-frame identity; require current state on every new row.

    This is only a source-generation precondition. It does not transfer the old
    pixel/state pass to the new DLL. The shared runner must still execute the
    original draw and Prepared.qualify with the current SHA for every row.
    """
    retained_sha='5cd20e685ea9089f9387056014857b4870d97878acddd6661aab9b16ce7d534f'
    old_library_sha='9fdec5189881eabf77d00a52a8442ee57f5f2fe8199d7c22335594ee28cedf95'
    inputs_sha='e8f3fa0c4dd3aaff0efa4f555bcd143a2f0c67255e0e58994ffe9fd6aa119bd9'
    require(sha(Path(args.source_report).read_bytes())==retained_sha,'Wrong retained composition report digest')
    require(source_report.get('passed') is True and
        source_report.get('original_composed_all_draws_executed') is True and
        source_report.get('library_sha256')==old_library_sha and
        source_report.get('inputs_sha256')==inputs_sha and
        coverage.get('inputs_sha256')==inputs_sha and
        coverage.get('ledger_sha256')=='6d600bc6b0f04f04b0950e94541797a549b5d35ef500d6bc592cc99eb7121dce' and
        source_report.get('selected_draws')==4331 and len(source_report.get('draws',[]))==4331 and
        [row.get('input') for row in source_report['draws']]==list(range(1,4332)) and
        all(row.get('shader_state_components_qualified') is True for row in source_report['draws']),
        'Wrong/missing retained whole-frame state/input pass')
    require(sha(Path(args.library).read_bytes())==LIBRARY_SHA and
        sha(Path(args.source).read_bytes())==SOURCE_SHA,'Wrong current source/DLL generation')
    old_library=Path('build/visibility-lle/libxlle_nv2a_reference.dll')
    old_source=ROOT/'producer-color-clamp-old-source.aot-private.cpp'
    require(sha(old_library.read_bytes())==old_library_sha and sha(old_source.read_bytes())==
        '24669d0fc03643e001373314d03258059777f441865ecfa846e4435b67b9af86',
        'Changed retained source/PE generation')
    old_evidence=surviving_software_evidence(old_library,old_source)
    current_evidence=surviving_software_evidence(Path(args.library),Path(args.source))
    require(len(old_evidence['code_sites'])==40 and len(current_evidence['code_sites'])==65 and
        old_evidence['plane_arithmetic']=='binary32 distance, difference and division; FMA intersection' and
        current_evidence['clip_arithmetic']=='fma' and current_evidence['depth_arithmetic']=='fma-b-first' and
        {'depth_b_rounded_product','depth_fused_a_plus_b','depth_fused_c_addition'}<=
            {row['semantic'] for row in old_evidence['code_sites']},
        'Unreviewed old/current clipping/raster/depth generation')
    require(sha((ROOT/'producer-color-clamp-generation-root-review.aot-private.json').read_bytes())==
        'fac4d5515de39c97344251ca497d3c39fb10481c6fbaba6cc4056d88983ecf18',
        'Changed independent reviewed source/PE transition')
    return dict(retained_source_report_sha256=retained_sha,retained_source_library_sha256=old_library_sha,
        current_library_sha256=LIBRARY_SHA,current_source_sha256=SOURCE_SHA,
        retained_all_frame_state_draws=4331,current_all_frame_state_requalified=False,
        old_current_code_sites=[40,65],current_state_validation_required_for_every_prefix_row=True)


def record_current_prefix_state(report):
    require(report.get('passed') is True and len(report.get('rows',[]))==report.get('draws') and
        [row.get('input') for row in report['rows']]==list(range(1,report['draws']+1)) and
        all(row.get('qualified') is True and row.get('shader_state_components_qualified') is True
            for row in report['rows']),'A current original state/vertex/pixel row failed')
    report.update(current_generation_original_state_qualified_prefix=report['draws'],
        current_generation_all4331_state_qualified=False)


def expected_input2129_draw(capture,scene,vertices,fog,targets,before,depth_arithmetic,clip_arithmetic):
    clamped = clamp_vertices(vertices)
    triangles,stats = clipped_triangles(clamped,*scene['before'][0]['shape'][:2],clip_arithmetic)
    errors = validated2129_errors(capture,scene,vertices,fog,targets,before)
    if triangles:
        bounds = precision_domain(errors,clamped)
    else:
        # Complete clipping proves that no primary color or palette error is
        # consumed. Retain exact input errors without imposing a sampled bound.
        bounds = [e*Fraction(max(v[c] for v in clamped)) for e,c in zip(errors,(4,5,6))]
    methods,fixture,pixel = expected_delivery(capture,scene)
    methods = {**methods,**fixture,**pixel};consumed_profile(methods)
    if not triangles:validate_empty_inputs(capture,scene,clamped,fog,targets,before,methods)
    images = bc1_images(scene['textures'][0])
    storage = dict(before);changed = set();covered = tested = rejected = counted = 0;error = 0
    metrics = {'maximum_consumed_diffuse':0}
    for triangle in triangles:
        part_capture,part_scene = copy.deepcopy((capture,scene))
        part_capture['header'][4] = part_scene['header'][5] = 3
        half = [list(v) for v in triangle]
        for v in half:
            for lane in (4,5,6):v[lane] /= 2
        storage,writes,row = expected_draw(part_capture,part_scene,half,[0.,0.,0.],
                                          targets,storage,depth_arithmetic)
        color_id = scene['frame_resource_plan']['target_ids'][0]
        color = np.frombuffer(storage[color_id],dtype='<u4').copy()
        for index,rgb in triangle_colors(triangle,images,methods,scene['before'][0]['shape'][0],writes,metrics).items():
            color[index] = (int(color[index])&0xff000000) | sum(v << s for v,s in zip(rgb,(16,8,0)))
        storage[color_id] = color.tobytes()
        changed.update(writes);covered += row['covered_pixels'];tested += row['tested_fragments']
        rejected += row['rejected_depth_fragments'];counted += row['triangles']
        error = max(error,row['maximum_affine_depth_error'])
    return storage,changed,dict(**stats,triangles=counted,tested_fragments=tested,
                               rejected_depth_fragments=rejected,maximum_affine_depth_error=error,
                               covered_pixels=covered,expected_written_pixels=len(changed),**metrics,
                               clamp_order=CLAMP_ORDER,input_codec_error_codes=list(map(str,errors)),
                               input_prequantization_codec_error_codes=list(map(str,bounds)),
                               codec_precision_unconsumed_because_no_raster_leaves=not triangles,
                               clamped_input_components=sum(a[c] != b[c] for a,b in zip(vertices,clamped) for c in (4,5,6)),
                               rgb_channel_bound=1)

_nominal_input2129=expected_input2129_draw


def expected_sampled_bounded_draw(capture,scene,vertices,fog,targets,before,depth_arithmetic,clip_arithmetic):
    if capture.get('input_sequence')!=2129:
        return expected_bounded_surviving_draw(capture,scene,vertices,fog,targets,before,
            depth_arithmetic,clip_arithmetic)
    require(depth_arithmetic=='fma-b-first' and clip_arithmetic=='fma','Wrong2129 clip/depth graph')
    result,changed,row=_nominal_input2129(capture,scene,vertices,fog,targets,before,
        depth_arithmetic,clip_arithmetic)
    proof=validate_frozen_proof()
    require(len(changed)==582,'Wrong independently written original2129 footprint')
    row.update(sampled_precision_component=True,sampled_precision_proof_sha256=COMPLETE_PROOF_SHA,
        complete_prequantization_rgb_error_codes=proof['channel_total_error_codes'])
    return result,changed,row
