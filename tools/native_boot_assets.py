#!/usr/bin/env python3
"""Extract a private native-startup data pack from an admitted owned kernel.

No ROM, sample, mesh or texture bytes are part of this tool. The current
admission is deliberately narrow: the complete 4983 kernel inspected for this
port, not the shorter dump used solely for native save signing. Output is for
the MIT xbox-boot-re reconstruction; it is not a bootable firmware image.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

KERNEL_BYTES = 710720
KERNEL_SHA256 = "be30b43eecc31d46459bb2b0fb57d117dbc7bd8b56a23b369c6eaed48c16d86d"
BASE = 0x80010000
MAX_INPUT_BYTES = 2 * 1024 * 1024
SCHEMA = "xboxllr.native-boot-assets.v1"
DONOR_COMMIT = "12a3396aa77f72c8d2fd94c0988c7e11539b47e7"

# Logical 4627 address consumed by the reconstructed core, owned 4983
# address, length, canonical SHA-256. These are fingerprints, not ROM bytes.
RANGES = (
    ("gp_program", 0x80053568, 0x80067F68, 0x1894,
     "f3f4c38c2af16b45d583eeaf36feafc905baa76063b92e0f55c0beae7559c76e"),
    ("asin_factors", 0x80066030, 0x8007AA40, 0xC4,
     "5771b2f1106de7aaa9f27d216568adb10c6f3235163039aac650c02155786a98"),
    ("pcm_80066ac0", 0x80066AC0, 0x8007B4D0, 0x5540,
     "377dbdd241ffe9ecfb077a3683aec1f70fa291fe2dfef03a95755ab4723ac767"),
    ("pcm_8006c008", 0x8006C008, 0x80080A18, 0xEB8,
     "4fdb4ec98442fa6571009e75be961e1ac2ad5d3be7c8a84ecfc745a179bc4789"),
    ("pcm_8006cec8", 0x8006CEC8, 0x800818D8, 0x1A3F,
     "daf78b106580a3977bd80bcec12096dc590a881529ff404ddaa89acd35de3f32"),
    ("sequence", 0x8006E908, 0x80083318, 0x6F8,
     "e6f82a5681e81f03f6a6618ef654da3c85b1a6134099c2e6086e20b5d707fd02"),
    ("campath", 0x80070DB8, 0x800857D8, 0x24C,
     "172d5fae12dafb80ebb1b49add2086d686cb3dee1632c8b74b218e3d6621d18e"),
    ("logo", 0x80071008, 0x80085A28, 0x33C8,
     "8f44555cc8f97935447d74adda8e9b5f7edbec5d8f717558c19763837b09f2fb"),
    ("scene", 0x800743D0, 0x80088DF0, 0x4134,
     "760e79659736bb9f6213c52927b4a5bf64e5bddfa63ce6e8cfe5c4030a1560a4"),
    ("envblocks", 0x800A7980, 0x800BC3A0, 0x230,
     "5dd50d15ba30fb9a1d2e433650a3066f14f647255dfad17fed0b5330f6629e06"),
    ("seq_pointers", 0x800A7BB0, 0x800BC5D0, 0x64,
     "743a4bc9011609fc085ac2f09112ab82fb177ddd37adc43c34906d3f55730c07"),
)


class AssetError(ValueError):
    """The supplied input is unsupported or failed integrity checks."""


def sha256(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest()


def _relocate_sequence(block: bytes) -> bytes:
    result = bytearray(block)
    # Header function-pointer tables are dead data to the reconstruction:
    # Audio.c reads descriptors, track bytecode and events from +0x210 onward.
    # Omit these unused addresses instead of inventing code equivalence.
    result[4:0x210] = bytes(0x20C)
    tables = {0x800B6598, 0x800A8598, 0x80098590, 0x80097D48,
              0x8008D2B8, 0x800ABB18, 0x800BA5A0, 0x800A8698}
    for offset in range(0x210, len(result) - 3, 2):
        value, = struct.unpack_from("<I", result, offset)
        if 0x80083318 <= value < 0x80083A10:
            value -= 0x14A10   # descriptors and track jumps
        elif 0x800BC3A0 <= value < 0x800BC634 or value in tables:
            value -= 0x14A20   # envelope/event lists and generated sample tables
        else:
            continue
        struct.pack_into("<I", result, offset, value)
    return bytes(result)


def _relocate_pointers(block: bytes) -> bytes:
    result = bytearray(block)
    # Eleven descriptor pointers, thirteen track pointers, final event max.
    for offset in range(0, 96, 4):
        value, = struct.unpack_from("<I", result, offset)
        if not 0x80083318 <= value < 0x80083A10:
            raise AssetError("Audio pointer table is outside the admitted sequence")
        struct.pack_into("<I", result, offset, value - 0x14A10)
    return bytes(result)


def extract(kernel: bytes) -> tuple[bytes, dict]:
    if len(kernel) != KERNEL_BYTES or sha256(kernel) != KERNEL_SHA256:
        raise AssetError("Unsupported kernel. This preview needs the complete admitted "
                         "4983 kernel; a save-signing kernel dump is insufficient.")
    entries, blobs, inventory = [], [], []
    offset = (16 + 32 * len(RANGES) + 15) & ~15
    for name, logical, owned, size, digest in RANGES:
        begin = owned - BASE
        block = kernel[begin:begin + size]
        if len(block) != size:
            raise AssetError(f"Truncated startup range: {name}")
        if name == "sequence":
            block = _relocate_sequence(block)
        elif name == "seq_pointers":
            block = _relocate_pointers(block)
        actual = sha256(block)
        if digest and actual != digest:
            raise AssetError(f"Startup range integrity failed: {name}")
        entries.append(struct.pack("<IIII16s", logical, size, offset, 0,
                                   name.encode("ascii")))
        blobs.append((offset, block))
        inventory.append({"name": name, "bytes": size, "sha256": actual})
        offset = (offset + size + 15) & ~15
    pack = bytearray(offset)
    pack[:16] = b"XBPK" + struct.pack("<III", 1, len(RANGES), offset)
    pack[16:16 + 32 * len(RANGES)] = b"".join(entries)
    for begin, block in blobs:
        pack[begin:begin + len(block)] = block
    manifest = {"schema": SCHEMA, "renderer": "native-reconstruction",
                "kernel_version": "4983", "kernel_sha256": KERNEL_SHA256,
                "kernel_bytes": len(kernel), "core_commit": DONOR_COMMIT,
                "pack_bytes": len(pack), "pack_sha256": sha256(pack),
                "ranges": inventory,
                "audio": "sequencer and GP reconstruction; approximate voice synthesis",
                "firmware_execution": False}
    return bytes(pack), manifest


def read_kernel(path: Path) -> bytes:
    if path.is_symlink() or not path.is_file():
        raise AssetError("Choose a regular complete kernel file")
    with path.open("rb") as stream:
        data = stream.read(MAX_INPUT_BYTES + 1)
    if len(data) > MAX_INPUT_BYTES:
        raise AssetError("Kernel file exceeds the input size limit")
    return data


def write_pack(kernel: Path, destination: Path) -> dict:
    pack, manifest = extract(read_kernel(kernel))
    source = kernel.resolve(strict=True)
    destination = destination.resolve()
    if destination == source or source.is_relative_to(destination):
        raise AssetError("Output must be separate from the supplied kernel")
    # Refuse an existing output rather than overwrite a user's data or profile.
    destination.mkdir(parents=True, exist_ok=False)
    # A failed write preserves partial output for diagnosis; never recursively
    # delete a user-selected path in an error handler.
    (destination / "xboot.pak").write_bytes(pack)
    (destination / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        result = write_pack(args.kernel, args.output)
    except (AssetError, OSError) as exc:
        parser.exit(2, f"Startup assets were not created: {exc}\n")
    print(json.dumps({k: result[k] for k in
                     ("schema", "pack_bytes", "pack_sha256", "renderer")}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
