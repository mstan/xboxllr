"""Input-only Fraction controls for float blending followed by byte storage.

This authored oracle is separate from captured scene qualification. Its error
interval is conditional on independently checked float-source/filter premises;
renderer observations never choose either the center or the interval.
"""
from fractions import Fraction as F
import json


def pack(value):
    value = min(F(1), max(F(0), F(value)))
    return (value * 255 + F(1, 2)).numerator // (value * 255 + F(1, 2)).denominator


def source(texels, weights=(F(3, 16), F(9, 16), F(1, 16), F(3, 16)), primary=F(64, 255)):
    if len(texels) != 4 or len(weights) != 4 or sum(weights) != 1 or min(weights) < 0:
        raise ValueError('four positive normalized taps required')
    # Each of four identical PROJECT2D samples contributes sample * primary.
    filtered = [sum(F(texels[t][c], 255) * weights[t] for t in range(4)) for c in range(4)]
    return [min(F(1), max(F(0), 4 * primary * channel)) for channel in filtered]


def blend(color, destination):
    alpha = color[3]
    return [channel * alpha + F(destination[c], 255) * (1 - alpha) for c, channel in enumerate(color)]


def fixtures():
    definitions = [
        ('A', [(1, 1, 1, 64), (2, 2, 2, 64), (1, 1, 1, 65), (2, 2, 2, 65)], [0, 0, 0, 255]),
        ('B', [(127, 127, 127, 12), (127, 127, 127, 12), (127, 127, 127, 13), (127, 127, 127, 13)], [16, 16, 16, 255]),
    ]
    # 640 binary32 ulps at one. The admitted quarter coordinates are exact;
    # conversion <=4u, filtering <=16u, four tap arithmetic <128u, blend
    # <512u and final packing/normalization <128u are separate premises.
    error = F(640, 1 << 23)
    result = []
    for name, texels, destination in definitions:
        color = source(texels)
        raw = blend(color, destination)
        packed_source = [F(pack(c), 255) for c in color]
        row = dict(name=name, texels=texels, destination=destination,
                   source=[str(v) for v in color], blended=[str(v) for v in raw],
                   rgba=[pack(v) for v in raw],
                   conditional_interval=[[pack(v-error), pack(v+error)] for v in raw],
                   source_packed_counterfactual=[pack(v) for v in blend(packed_source, destination)])
        result.append(row)
    return dict(schema='authored-float-blend-v1', source_order='float source, blend, final byte pack',
                conditional_error=str(error), quarter_weights=['3/16', '9/16', '1/16', '3/16'], fixtures=result)


if __name__ == '__main__':
    print(json.dumps(fixtures(), indent=2))
