"""Attach the authored native texture-view cache after donor sampler binding."""
def prepare_texture_views(code):
    old='            d3d8_states_apply_sampler(s);\n    }\n}'
    if code.count(old)!=1:
        raise ValueError('unreviewed sampler/view binding boundary')
    return ('void pilot_d3d_texture_sampling_views(void);\n'+
            code.replace(old,'            d3d8_states_apply_sampler(s);\n    }\n'
                             '    pilot_d3d_texture_sampling_views();\n}'))
