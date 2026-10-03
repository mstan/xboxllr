#!/usr/bin/env python3
"""Build an authored 257-region branch cycle through the production compiler."""
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import aot_compile


def generate(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    regions = []
    count, base, stride = 257, 0x10000, 16
    for i in range(count):
        pc = base + i * stride
        destination = base + ((i + 1) % count) * stride
        # MOV EAX,index; JMP next region. No captured Xbox code.
        code = b"\xb8" + i.to_bytes(4, "little") + b"\xe9"
        code += ((destination - (pc + 10)) & 0xffffffff).to_bytes(4, "little")
        regions.append({"name": f"registry_{i}", "base": pc, "code": code.hex()})
    source = directory / "registry-input.json"
    source.write_text(json.dumps({"regions": regions}), encoding="utf-8")
    c, h = directory / "registry.c", directory / "registry.h"
    aot_compile.compile_input(source, c, h, directory / "registry-manifest.json")
    # Test-only index over production-generated symbols; no generated guest body edits.
    with c.open("a", encoding="utf-8") as out:
        out.write("\nconst xlle_aot_region* const registry_regions[] = {\n")
        out.write("".join(f"    &xlle_aot_registry_{i},\n" for i in range(count)))
        out.write("};\n")
    with h.open("a", encoding="utf-8") as out:
        out.write('\n#ifdef __cplusplus\nextern "C" {\n#endif\n')
        out.write(f"extern const xlle_aot_region* const registry_regions[{count}];\n")
        out.write("#ifdef __cplusplus\n}\n#endif\n")


if __name__ == "__main__":
    generate(Path(sys.argv[1]))
