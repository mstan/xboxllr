"""Import the standalone HLE renderer's shader translators, from pinned Git objects."""
import pathlib
import re
import subprocess
import sys

PIN = "482088490c2d5983ed5e1eafbd09906157c1eca0"
root = pathlib.Path(__file__).resolve().parents[1]
donor = pathlib.Path(sys.argv[1]).resolve()
base = "hw/xbox/nv2a/pgraph/"


def source(path):
    return subprocess.check_output(["git", "-C", str(donor), "show", f"{PIN}:{base}{path}"], text=True)


def save(name, text):
    dest = root / "src/third_party/xemu" / name
    dest.write_bytes((text.rstrip() + "\n").encode())


vsh = source("glsl/vsh-prog.c")
vsh = vsh.replace('#include "qemu/osdep.h"', '#include "mstring.h"')
vsh = vsh.replace('#include "vsh.h"', '#include "vsh_regs.h"')
vsh = re.sub(r'            assert\(!"TODO: Emulate writeable const registers"\);.*?(?=        } else)',
             '            abort(); /* Rejected by the HLE adapter: no placeholder constant writes. */\n',
             vsh, flags=re.S)
save("vsh-prog.c", vsh)
save("vsh-prog.h", source("glsl/vsh-prog.h").replace('"qemu/mstring.h"', '"mstring.h"'))
regs = source("vsh_regs.h")
regs = re.sub(r'#include "(?:qemu/osdep.h|hw/xbox/nv2a/nv2a_regs.h)"\n', '', regs)
save("vsh_regs.h", regs)

# Share the already imported, placeholder-free combiner generator, but use a
# separate entry with the HLE renderer's own coverage. Do not change LLE gates.
psh = (root / "src/third_party/xemu/psh.c").read_text()
psh = psh.replace('DEF_UNIFORM_INFO_ARR(PshUniform, PSH_UNIFORM_DECL_X)', '')
psh = psh.replace('pgraph_glsl_gen_psh(', 'xlle_hle_gen_psh(')
psh = psh.replace('ARRAY_SIZE(PshUniformInfo)', 'PshUniform__COUNT')
start = psh.index('    if (!state ||')
end = psh.index('    if (state->other_stage_input & 0xfff) return NULL;', start)
end += len('    if (state->other_stage_input & 0xfff) return NULL;')
psh = psh[:start] + '''    if (!state || (state->combiner_control & 255) > 8 ||
        (state->depth_format != DEPTH_FORMAT_D24 && state->depth_format != DEPTH_FORMAT_D16) ||
        state->z_perspective) return NULL;
    for (int i = 0; i < 4; ++i) {
        unsigned mode = (state->shader_stage_program >> (i * 5)) & 31;
        // These modes have complete donor implementations. Others remain explicit errors.
        if (mode != PS_TEXTUREMODES_NONE && mode != PS_TEXTUREMODES_PROJECT2D &&
            mode != PS_TEXTUREMODES_PROJECT3D && mode != PS_TEXTUREMODES_CUBEMAP && mode != PS_TEXTUREMODES_PASSTHRU &&
            mode != PS_TEXTUREMODES_CLIPPLANE) return NULL;
        if (state->conv_tex[i] != CONVOLUTION_FILTER_DISABLED || state->snorm_tex[i] ||
            state->colorkey_mode[i]) return NULL;
    }
''' + psh[end:]
save("hle_psh.c", psh)

for name in ("swizzle.c", "swizzle.h"):
    save(name, source(name))
