"""Independent, closed post-transform rectangle pixel equations.

Inputs are explicit vertices, NV097 state words and supplied producer bytes.
No maintained renderer, shader compiler, decoder or after-image provides an
expectation. This qualifies consumers of supplied images, not their producers,
complete frames, enabled noise, feedback caches or exact silicon rounding.

Equations: NV_register_combiners[2], NV_texture_rectangle and the public nxdk
NV097 field definitions. The model uses ideal floating arithmetic and declares
a one-code UNORM8 comparison tolerance. LSB mux and undefined register reads
are guarded. MSB mux uses the documented ideal threshold (less than 0.5 versus
at least 0.5), independently of any observed result. Minimum threshold margin
is reported; this floating equation contract does not assert silicon precision.
"""
import numpy as np

SOURCES = [
    'https://registry.khronos.org/OpenGL/extensions/NV/NV_register_combiners.txt',
    'https://registry.khronos.org/OpenGL/extensions/NV/NV_register_combiners2.txt',
    'https://registry.khronos.org/OpenGL/extensions/NV/NV_texture_rectangle.txt',
    'https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/nv_regs.h',
    'https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#DefaultsForMissingComponents',
]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def rgba(data, width, height, pitch):
    require(type(data) is bytes and pitch >= width*4 and pitch%4 == 0 and len(data) == pitch*height,
            'Incomplete pitch-linear ARGB input')
    words = np.frombuffer(data, dtype='<u4').reshape(height, pitch//4)[:, :width]
    return np.stack([(words >> s) & 255 for s in (16, 8, 0, 24)], axis=-1)/255.


def quantize(values):
    require(np.isfinite(values).all(), 'Undefined/nonfinite pixel result')
    return np.floor(np.clip(values, 0, 1)*255+.5).astype(np.uint8)


def depth_only_view(data, width, height, pitch):
    """Counterfactual R24_UNORM_X8 SRV: depth R, absent G/B=0 and A=1.

    Input bytes are physical Xbox Z24S8. A native upload rearranges these bits
    into native D24S8; it does not turn that typed depth view into raw ARGB.
    This is a documented view equation, not a native GPU observation.
    """
    require(pitch%4 == 0 and pitch >= width*4 and len(data) == pitch*height,
            'Incomplete supplied depth storage')
    words = np.frombuffer(data,dtype='<u4').reshape(height,pitch//4)[:,:width]
    depth = (words >> 8)/16777215.
    return np.stack((depth,np.zeros_like(depth),np.zeros_like(depth),np.ones_like(depth)),axis=-1)


def stencil_bytes(methods, data, depth_width, depth_height, depth_pitch, width, height):
    """Closed ALWAYS/KEEP-or-REPLACE S8 contract over rectangle coverage."""
    require(methods[0x32c] in (0,1) and methods[0x30c] == methods[0x35c] == 0,
            'Unmodelled depth/stencil profile')
    require(depth_width >= width and depth_height >= height and depth_pitch%4 == 0 and
            depth_pitch >= depth_width*4 and len(data) == depth_pitch*depth_height,
            'Insufficient independent depth storage')
    if not methods[0x32c]:return data
    require(methods[0x364] == 0x207 and methods[0x378] in (0x1e00,0x1e01),
            'Only ALWAYS with KEEP/REPLACE stencil admitted')
    if methods[0x378] == 0x1e00:return data
    words = np.frombuffer(data,dtype='<u4').reshape(depth_height,depth_pitch//4).copy()
    mask = methods[0x360] & 255
    words[:height,:width] = (words[:height,:width] & np.uint32(0xffffffff ^ mask)) | (methods[0x368] & mask)
    return words.tobytes()


def mapping(value, operation):
    positive = np.maximum(value, 0)
    return (positive, 1-np.clip(value, 0, 1), 2*positive-1, 1-2*positive,
            positive-.5, .5-positive, value, -value)[operation]


def combiner(methods, textures, primary, secondary):
    """Vectorized register algebra; every stage reads the preceding register set."""
    control = methods[0x1e60]
    count = control & 255
    require(1 <= count <= 8 and control & ~0x111ff == 0,
            'Unmodelled combiner count/control flags')
    require((control >> 8) & 15 in (0, 1), 'Unmodelled mux selection')
    primary = np.asarray(primary, dtype=np.float64)
    secondary = np.asarray(secondary, dtype=np.float64)
    require(primary.shape == secondary.shape and primary.shape[-1] == 4 and
            np.isfinite(primary).all() and np.isfinite(secondary).all(),
            'Incomplete primary/secondary inputs')
    shape = primary.shape
    registers = {0: np.zeros(shape), 4: primary.copy(), 5: secondary.copy(),
                 12: np.full(shape, np.nan), 13: np.full(shape, np.nan)}
    for stage, texture in enumerate(textures):
        if texture is not None:
            require(texture.shape == shape and np.isfinite(texture).all(),
                    'Incomplete texture-register input')
            registers[8+stage] = texture.copy()
    if 8 in registers:
        registers[12][..., 3] = registers[8][..., 3]
    minimum_mux_margin = None

    def constant(word):
        return np.array([(word >> s) & 255 for s in (16, 8, 0, 24)])/255.

    def variable(byte, alpha, final=False):
        source = byte & 15
        require(source in registers, 'Unmodelled or unbound combiner register')
        require(not final or byte >> 5 in (0, 1), 'Unmodelled final input mapping')
        value = registers[source]
        value = value[..., 3:4] if byte & 16 else value[..., 2:3] if alpha else value[..., :3]
        if not alpha and byte & 16:
            value = np.repeat(value,3,axis=-1)
        require(np.isfinite(value).all(), 'Consumed undefined combiner register')
        return mapping(value, byte >> 5)

    for stage in range(count):
        for bank, base, flag in ((1, 0xa60, 12), (2, 0xa80, 16)):
            index = stage if control & (1 << flag) else 0
            registers[bank] = np.broadcast_to(constant(methods[base+4*index]), shape)
        writes = []
        for alpha, icw, ocw in ((False, 0xac0, 0x1e40), (True, 0x260, 0xaa0)):
            word = methods[ocw+4*stage]
            destinations = [(word >> s) & 15 for s in (4, 0, 8)]
            require(all(d in (0, 4, 5, 8, 9, 10, 11, 12, 13) for d in destinations),
                    'Unmodelled output register')
            active = [d for d in destinations if d]
            require(len(active) == len(set(active)), 'Overlapping combiner outputs')
            require(not word & (~0x3ffff if alpha else ~0xfffff), 'Unmodelled output flags')
            require(not alpha or not word & 0x3000, 'Alpha dot product is undefined')
            require(not word & 0x3000 or not destinations[2], 'Dot-plus-sum needs a separate contract')
            if not active:
                require(not word & 0xc0000, 'Blue-to-alpha without destination')
                continue
            inputs = methods[icw+4*stage]
            a, b, c, d = [variable((inputs >> s) & 255, alpha) for s in (24, 16, 8, 0)]
            ab, cd = a*b, c*d
            if word & 0x2000:
                ab = np.repeat(ab.sum(axis=-1, keepdims=True), 3, axis=-1)
            if word & 0x1000:
                cd = np.repeat(cd.sum(axis=-1, keepdims=True), 3, axis=-1)
            combined = ab+cd
            if word & 0x4000:
                require(control & 0x100, 'LSB mux requires a separate precision contract')
                selector = registers[12][..., 3:4]
                require(np.isfinite(selector).all(), 'Undefined mux selector')
                margin = float(np.min(abs(selector-.5)))
                minimum_mux_margin = margin if minimum_mux_margin is None else min(margin, minimum_mux_margin)
                combined = np.where(selector < .5, ab, cd)
            operation = (word >> 15) & 7
            require(operation in (0, 1, 2, 3, 4, 6), 'Unmodelled output scale/bias')
            scale, bias = {0:(1,0), 1:(1,-.5), 2:(2,0), 3:(2,-.5), 4:(4,0), 6:(.5,0)}[operation]
            for index, (destination, value) in enumerate(zip(destinations, (ab, cd, combined))):
                if not destination:
                    continue
                value = np.clip((value+bias)*scale, -1, 1)
                writes.append((destination, 3 if alpha else slice(0, 3), value))
                if not alpha and index < 2 and word & (1 << (19-index)):
                    writes.append((destination, 3, value[..., 2:3]))
        assigned = set()
        for destination, portion, value in writes:
            lanes = [portion] if isinstance(portion, int) else [0, 1, 2]
            for lane in lanes:
                require((destination, lane) not in assigned, 'Overlapping RGB/alpha writes')
                assigned.add((destination, lane))
            if isinstance(portion, int):
                registers[destination][..., portion] = value[..., 0]
            else:
                registers[destination][..., portion] = value

    registers[1] = np.broadcast_to(constant(methods[0x1e20]), shape)
    registers[2] = np.broadcast_to(constant(methods[0x1e24]), shape)
    cw0, cw1 = methods[0x288], methods[0x28c]
    require(not cw1 & 0x7f, 'Unmodelled specular-sum inversion/final flags')
    e, f = [variable((cw1 >> shift) & 255, False, True) for shift in (24, 16)]
    registers[15] = np.concatenate((e*f, np.zeros(shape[:-1]+(1,))), axis=-1)
    # Undefined RGB stays undefined until consumed by a final input.
    summed = np.maximum(registers[12][..., :3], 0)+np.maximum(registers[5][..., :3], 0)
    if cw1 & 128:
        summed = np.clip(summed, 0, 1)
    registers[14] = np.concatenate((summed, np.zeros(shape[:-1]+(1,))), axis=-1)
    a, b, c, d = [variable((cw0 >> s) & 255, False, True) for s in (24, 16, 8, 0)]
    g = variable((cw1 >> 8) & 255, True, True)
    result = np.clip(np.concatenate((a*b+(1-a)*c+d, g), axis=-1), 0, 1)
    require(np.isfinite(result).all(), 'Undefined final combiner output')
    return result, minimum_mux_margin


def rectangle(vertices, primitive, width, height):
    v = np.asarray(vertices, dtype=np.float64)
    require(v.shape == (4, 28) and np.isfinite(v).all(), 'Not a finite four-vertex rectangle')
    require(primitive in (6, 8), 'Only strip/quad rectangles admitted')
    order = [1, 3, 2, 0] if primitive == 6 else [0, 1, 2, 3]
    corners = v[order]
    require(np.array_equal(corners[:, :4], [[0,0,0,1], [width,0,0,1],
                                          [width,height,0,1], [0,height,0,1]]),
            'Rectangle coverage/depth/perspective outside contract')
    require(np.all(v[:, 4:12] == v[0, 4:12]), 'Varying colors outside contract')
    require(np.all((0 <= v[:, 4:12]) & (v[:, 4:12] <= 1)), 'Colors outside UNORM range')
    return corners


def sample(image, corners, stage, width, height, linear):
    coords = corners[:, 12+stage*4:16+stage*4]
    require(np.all(coords[:, 2] == 0) and np.all(coords[:, 3] == 1),
            'Nonunit projective coordinates')
    require(np.array_equal(coords[0, :2]+coords[2, :2], coords[1, :2]+coords[3, :2]),
            'Nonaffine rectangle texture coordinates')
    xs = (np.arange(width)+.5)/width
    ys = (np.arange(height)+.5)/height
    u = coords[0,0]+xs[None,:]*(coords[1,0]-coords[0,0])+ys[:,None]*(coords[3,0]-coords[0,0])
    v = coords[0,1]+xs[None,:]*(coords[1,1]-coords[0,1])+ys[:,None]*(coords[3,1]-coords[0,1])
    ih, iw = image.shape[:2]
    u = np.clip(u, .5, iw-.5); v = np.clip(v, .5, ih-.5)
    if not linear:
        return image[np.floor(v).astype(int), np.floor(u).astype(int)]
    u -= .5; v -= .5
    x, y = np.floor(u).astype(int), np.floor(v).astype(int)
    fx, fy = (u-x)[..., None], (v-y)[..., None]
    xx, yy = np.minimum(x+1, iw-1), np.minimum(y+1, ih-1)
    return ((1-fy)*((1-fx)*image[y,x]+fx*image[y,xx])+
            fy*((1-fx)*image[yy,x]+fx*image[yy,xx]))


def pixels(methods, vertices, primitive, width, height, images, before):
    corners = rectangle(vertices, primitive, width, height)
    expected = {0x200:width<<16, 0x204:height<<16, 0x208:0x128,
                0x2c0:(width-1)<<16, 0x2e0:(height-1)<<16,
                0x300:0, 0x308:0, 0x30c:0, 0x310:0, 0x318:0, 0x324:0,
                0x314:0, 0x320:0, 0x38c:0x1b02, 0x390:0x1b02,
                0x358:0x1010101, 0x35c:0, 0x37c:0x1d01}
    require(all(methods.get(k) == value for k, value in expected.items()),
            'Raster tests/masks/modes outside rectangle contract')
    require(methods.get(0x17bc) == 0 or (methods.get(0x17bc) == 1 and
            methods.get(0x17c0) == 0x1503 and methods[0x304] == 0),
            'Only disabled logic or unblended COPY admitted')
    require(len(images) == 4 and before.shape == (height, width, 4), 'Incomplete image inputs')
    require(methods[0x1e70] & ~0xfffff == 0, 'Unmodelled texture program flags')
    sampled = []
    for stage, image in enumerate(images):
        program = (methods[0x1e70] >> (5*stage)) & 31
        require(program == (1 if image is not None else 0), 'Only bound PROJECT2D stages admitted')
        if image is None:
            sampled.append(None)
            continue
        base = 0x1b00+stage*64
        require(methods[base+4] == 0x11229 and methods[base+8] == 0x10303 and
                methods[base+12] == 0x4003ffc0, 'Texture layout/address/kill outside contract')
        ih, iw = image.shape[:2]
        require(methods[base+28] == (iw<<16)|ih and image.shape[-1] == 4,
                'Texture dimensions differ')
        filtering = methods[base+20]
        minimum = (filtering >> 16) & 255
        require(filtering & 0xf0000000 == 0 and filtering >> 24 in (1, 2) and
                minimum in (1, 2, 5, 6), 'Unmodelled sampler filter/sign')
        # All admitted images have one mip level. Within this domain the
        # specified nearest/linear min and mag filters must agree.
        require((filtering >> 24) == (1 if minimum in (1,5) else 2),
                'Different min/mag filters need a LOD contract')
        sampled.append(sample(image, corners, stage, width, height, filtering >> 24 == 2))
    shape = (height, width, 4)
    color, margin = combiner(methods, sampled, np.broadcast_to(corners[0,4:8], shape),
                             np.broadcast_to(corners[0,8:12], shape))
    # The generic software LLE clamps the final combiner source, blends its
    # floating RGB/alpha, then packs the output to UNORM8. Its preblend packed
    # alpha is only an alpha-test operand; it is not the source blend factor.
    source = np.clip(color, 0, 1)
    require(methods[0x304] in (0, 1), 'Unmodelled blend enable')
    if methods[0x304]:
        require(methods[0x344] == 0x302 and methods[0x348] == 0x303 and
                methods[0x350] == 0x8006, 'Only source-alpha blending admitted')
        source = source*source[...,3:4]+before*(1-source[...,3:4])
    return quantize(source), margin


def compare(expected, actual):
    require(expected.shape == actual.shape and expected.dtype == actual.dtype == np.uint8,
            'Incomplete actual pixel observation')
    difference = abs(expected.astype(int)-actual.astype(int))
    return dict(component_pixels_qualified=bool(np.all(difference <= 1)),
                compared_pixels=int(np.prod(expected.shape[:-1])), compared_channels=expected.size,
                tolerance_unorm8_codes=1, maximum_difference=int(difference.max()),
                outside_interval_channels=int(np.count_nonzero(difference > 1)))
