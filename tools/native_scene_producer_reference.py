"""Independent expectations for a closed, retained scene-producer prefix.

Reuse authored BC1, perspective, top-left and exact binary32 depth equations;
neither observed vertices/methods nor after-images supply these expectations.
All draws are opaque filled strips. Line width is unconsumed by this profile,
although original state delivery still has to match its input-derived value.
The reused closed equation contract admits repeat/trilinear BC1, RGB diffuse
in [0,0.5], saturated alpha and density-zero fog, zero specular, LESS depth,
RGB/depth writes and preserved target alpha/stencil. Exact silicon precision
and viewport/depth clipping of surviving faces remain separate contracts.
"""
import copy
import numpy as np

from native_frame_inputs import require
from native_frame_target_reference import layout
from native_frame_state_delivery import expected_delivery
from native_scene_pixel_reference import expected_targets


def native_depth(data):
    """Physical NV Z24S8 into the explicit native supplied-input representation."""
    words = np.frombuffer(data, dtype='<u4')
    return ((words >> 8) | ((words & 255) << 24)).astype('<u4').tobytes()


def producer_domain(vertices, width, height, cull=None):
    require(len(vertices) >= 3 and all(len(v) == 28 and np.isfinite(v).all() and
            0 <= v[2] <= 16777215 and v[3] > 0 for v in vertices),
            'Viewport/depth clipping requires a separate producer contract')
    require(cull in (None,0x404,0x405), 'Unknown filled-strip cull convention')
    active=set(range(len(vertices))) if cull is None else set()
    if cull is not None:
        for j in range(len(vertices)-2):
            indices=[j,j+1,j+2] if j%2==0 else [j+1,j,j+2]
            a,b,c=[vertices[i] for i in indices]
            det=(b[0]-a[0])*(c[1]-a[1])-(c[0]-a[0])*(b[1]-a[1])
            if det and (det>0)!=(cull==0x404):active.update(indices)
    # Positive W and admitted depth leave only convex XY clipping. Clipping
    # cannot reverse face orientation, so fully culled faces preserve targets.
    require(all(0 <= vertices[i][0] <= width and 0 <= vertices[i][1] <= height for i in active),
            'Viewport/depth clipping requires a separate producer contract')


def expected_draw(capture, scene, vertices, fog, targets, before, arithmetic):
    methods, fixture, pixel = expected_delivery(capture, scene)
    methods = {**fixture, **methods, **pixel}
    require(methods[0x38c] == methods[0x390] == 0x1b02 and
            capture['header'][2] == 6, 'Only filled triangle strips admitted')
    require(methods[0x308] == 1, 'Producer requires the admitted face-cull mode')
    producer_domain(vertices,*scene['before'][0]['shape'][:2],methods[0x39c])
    require(type(methods[0x380]) is int and 0 <= methods[0x380] <= 511,
            'Incomplete input-derived line width')
    # The older independent equation fixture requires a reset line-width word.
    # Filled triangles consume no line width. Normalize only that unconsumed
    # word here; the exact original word remains in Prepared's state audit.
    line_width = methods[0x380]
    methods[0x380] = 0
    pixel_scene = copy.deepcopy(scene)
    pair = scene['frame_resource_plan']['target_ids']
    for slot, target_id in enumerate(pair):
        width, height, pitch = layout(targets[target_id])
        require(pitch == width*4, 'Padded active producer target outside contract')
        require(pixel_scene['before'][slot]['shape'][2] in ((44,45) if slot else (87,)),
                'Producer target representation outside contract')
        pixel_scene['before'][slot]['data'] = native_depth(before[target_id]) if slot else before[target_id]
    supplied = dict(noise_disabled_fixture=True, complete_draw_executed=True,
                    vertex_capture_sha256=capture['sha256'], scene_capture_sha256=scene['sha256'],
                    vertices=vertices, fog=fog, methods={hex(k):v for k,v in methods.items()})
    _, _, colors, depths, covered, stats = expected_targets(
        capture, pixel_scene, supplied, arithmetic, allow_empty=True)
    result = dict(before)
    color, depth = pair
    cw = np.frombuffer(before[color],dtype='<u4').copy()
    zw = np.frombuffer(before[depth],dtype='<u4').copy()
    for index, rgba in colors.items():
        require(all(type(v) is int and 0 <= v <= 255 for v in rgba), 'Invalid expected color')
        cw[index] = sum(v << shift for v,shift in zip(rgba,(16,8,0,24)))
        lo, hi = depths[index]
        require(lo == hi and 0 <= lo <= 0xffffff, 'Expected exact software depth required')
        zw[index] = (lo << 8) | (int(zw[index]) & 255)
    result[color] = cw.tobytes()
    result[depth] = zw.tobytes()
    return result, set(colors), dict(**stats, covered_pixels=len(covered),
                                    expected_written_pixels=len(colors), unconsumed_line_width=line_width)


def compare_storage(targets, expected, actual, written):
    """One RGB code only at independently written pixels; every other byte exact."""
    require(set(expected) == set(actual) == set(targets), 'Missing owned target observation')
    rows = []
    for target_id, target in targets.items():
        require(len(actual[target_id]) == len(expected[target_id]), 'Incomplete target observation')
        expected_words = np.frombuffer(expected[target_id], dtype='<u4')
        actual_words = np.frombuffer(actual[target_id], dtype='<u4')
        require(expected_words.size == layout(target)[2]//4*target['height'], 'Bad expected target extent')
        admitted = written.get(target_id,set()) if not target['role'] else set()
        width,height,pitch = layout(target)
        require(all(type(i) is int and 0 <= i < expected_words.size and i%(pitch//4)<width
                    for i in admitted), 'Invalid written-pixel set')
        exact = np.ones(expected_words.size,dtype=bool)
        if admitted:exact[list(admitted)] = False
        exact_failures = int(np.count_nonzero((expected_words != actual_words) & exact))
        alpha_failures = rgb_failures = maximum = 0
        if admitted:
            wanted,got = expected_words[list(admitted)],actual_words[list(admitted)]
            alpha_failures = int(np.count_nonzero((wanted >> 24) != (got >> 24)))
            for shift in (0,8,16):
                delta = abs(((wanted >> shift)&255).astype(np.int16)-((got >> shift)&255).astype(np.int16))
                maximum = max(maximum,int(delta.max()))
                rgb_failures += int(np.count_nonzero(delta > 1))
        rows.append(dict(target=target_id,depth_stencil=bool(target['role']),
                         pixels=int(expected_words.size),exact_pixels=int(exact.sum()),
                         rgb_interval_pixels=len(admitted),maximum_rgb_difference=maximum,
                         exact_pixel_failures=exact_failures,alpha_failures=alpha_failures,
                         rgb_interval_channel_failures=rgb_failures,
                         qualified=not (exact_failures or alpha_failures or rgb_failures)))
    return dict(qualified=all(r['qualified'] for r in rows),targets=rows,
                compared_pixels=sum(r['pixels'] for r in rows),
                exact_pixels=sum(r['exact_pixels'] for r in rows))
