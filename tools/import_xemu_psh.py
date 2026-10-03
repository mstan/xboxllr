"""Refresh the pure pixel-shader translator from the explicitly pinned xemu tree.

Usage: py -3 tools/import_xemu_psh.py ../refs/xemu
The machine-state/uniform collectors are deliberately not imported. The host
adapter supplies real owned draw state; no QEMU device compatibility shims exist.
"""
import pathlib
import re
import subprocess
import sys

PIN = "482088490c2d5983ed5e1eafbd09906157c1eca0"
root = pathlib.Path(__file__).resolve().parents[1]
donor = pathlib.Path(sys.argv[1]).resolve()
if subprocess.check_output(["git", "-C", str(donor), "rev-parse", "HEAD"], text=True).strip() != PIN:
    raise SystemExit("unexpected xemu revision")


def source(path):
    # Read the pinned object, independent of any donor working-tree edits.
    return subprocess.check_output(["git", "-C", str(donor), "show", f"{PIN}:{path}"], text=True)


def save(name, contents):
    target = root / "src/third_party/xemu" / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(contents.rstrip() + "\n", encoding="utf-8", newline="\n")


base = "hw/xbox/nv2a/pgraph/"
psh = source(base + "glsl/psh.c")
notice = psh[:psh.index('#include')]
body = psh[psh.index('struct InputInfo {'):psh.index('void pgraph_glsl_set_psh_uniform_values')]
# These three names need only a bounded stack buffer, not GLib ownership.
body = re.sub(r'g_autofree gchar \*normalize_tex_coords = g_strdup_printf\("norm%d", (i|tex)\);',
              r'char normalize_tex_coords[32];\n    snprintf(normalize_tex_coords, sizeof(normalize_tex_coords), "norm%d", \1);', body)
# Do not incorporate upstream placeholder implementations, even as unused GLSL.
body = re.sub(r'        "vec3 dotmap_hilo_hemisphere_d3d.*?(?=        // Kahan)', '', body, flags=re.S)
for mode in ("BRDF", "DOT_ZW", "DOT_RFLCT_SPEC_CONST"):
    body = re.sub(r'        case PS_TEXTUREMODES_' + mode + r':.*?(?=        (?:case |default:))',
                  '        case PS_TEXTUREMODES_' + mode + ':\n            abort(); /* Rejected before generation; no placeholder output. */\n\n',
                  body, flags=re.S)
# Unexpected unsupported generation is a hard error, never a logged success.
body = re.sub(r'NV2A_UNIMPLEMENTED\([^;]+;', 'abort(); /* Unsupported, never continue with a fabricated value. */', body)
# Keep the actual Gaussian algorithm, but never substitute it for quincunx.
body = body.replace('// FIXME: Quincunx',
    'if (ps->state->conv_tex[tex] != CONVOLUTION_FILTER_GAUSSIAN) abort();')
body = body.replace('''        // TODO: handle floating-point depth buffers properly
        mstring_append(ps->code, "gl_FragDepth = zvalue / clipRange.y;\\n");''',
    '        abort(); /* Floating-point depth is unsupported, not approximated. */')
# Upstream omitted releasing this temporary generated source string.
body = body.replace('    mstring_unref(preflight);', '    mstring_unref(clip);\n    mstring_unref(preflight);')
# Standalone entry remains defensive independently of the C++ packet adapter.
guard = '''
    if (!state || (state->combiner_control & 0xff) > 8 ||
        !(state->final_inputs_0 || state->final_inputs_1) ||
        state->depth_format != DEPTH_FORMAT_D24 || state->z_perspective)
        return NULL;
    for (int unit = 0; unit < 4; ++unit) {
        unsigned mode = (state->shader_stage_program >> (5 * unit)) & 31;
        if (mode != PS_TEXTUREMODES_NONE && mode != PS_TEXTUREMODES_PROJECT2D)
            return NULL;
        if (state->conv_tex[unit] != CONVOLUTION_FILTER_DISABLED ||
            state->snorm_tex[unit] || state->shadow_map[unit]) return NULL;
    }
    if (state->other_stage_input & 0xfff) return NULL;
'''
entry = 'MString *pgraph_glsl_gen_psh(const PshState *state, GenPshGlslOptions opts)\n{'
assert entry in body
body = body.replace(entry, entry + guard)
save("psh.c", notice + '// Adapted by xboxlle: see README.md in this directory.\n#include "psh.h"\n\nDEF_UNIFORM_INFO_ARR(PshUniform, PSH_UNIFORM_DECL_X)\n\n' + body)

header = source(base + "glsl/psh.h").replace('"hw/xbox/nv2a/pgraph/psh_regs.h"', '"psh_regs.h"')
header = re.sub(r'void pgraph_glsl_set_psh_state\([^;]+;\n', '', header)
header = re.sub(r'void pgraph_glsl_set_psh_uniform_values\([^;]+;\n', '', header)
save("psh.h", header)
save("psh_regs.h", source(base + "psh_regs.h"))
common = source(base + "glsl/common.h")
common = common.replace('#include "qemu/osdep.h"\n#include "qemu/mstring.h"', '#include "mstring.h"')
common = re.sub(r'void pgraph_glsl_set_clip_range_uniform_value\([^;]+;\n', '', common)
save("common.h", common)
common = source(base + "glsl/common.c")
notice = common[:common.index('#include')]
body = common[common.index('#define DECL_UNIFORM_ELEMENT_NAME'):common.index('void pgraph_glsl_set_clip_range_uniform_value')]
save("common.c", notice + '#include "common.h"\n\n' + body)
