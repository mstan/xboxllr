#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Create a fresh, local-only Xbox EEPROM image for xboxlle.

Portions are a Python port of xemu/QEMU's hw/xbox/eeprom_generation.c
from xemu commit 482088490c2d5983ed5e1eafbd09906157c1eca0.

Original copyright:
  Copyright (c) 2020 Mike Davis
  Copyright (c) 2024 Ryan Wendland

Python port modifications copyright (c) 2026 Matthew Stan, under the same LGPL terms.

This modified Python port is licensed under the GNU Lesser General Public
License version 2 or later. This library is distributed WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. You should have received a copy of the GNU Lesser General
Public License along with this file; if not, see <https://www.gnu.org/licenses/>.

It is a local input generator; it does not embed or print private console
EEPROM contents, keys, firmware, or dashboard material.
"""
from __future__ import annotations

import argparse
import os
import secrets
import stat
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Optional, Union

EEPROM_SIZE = 256
_SECURITY_OFFSET = 20
_SECURITY_SIZE = 0x1C
_SERIAL_OFFSET = 52
_SERIAL_CHECKSUM_SIZE = 0x2C
_USER_CHECKSUM_OFFSET = 96
_USER_SECTION_OFFSET = 100
_USER_CHECKSUM_SIZE = 0x5C
_RANDOM_BYTES_REQUIRED = 8 + 16 + 16 + 6 + 12
_DEFAULT_REGION_NORTH_AMERICA = 1
_DEFAULT_VIDEO_NTSC_M = 0x00400100

_REVISION_STATES = {
    "debug": (
        (0x85F9E51A, 0xE04613D2, 0x6D86A50C, 0x77C32E3C, 0x4BD717A4),
        (0x5D7A9C6B, 0xE1922BEB, 0xB82CCDBC, 0x3137AB34, 0x486B52B3),
    ),
    "r1": (
        (0x72127625, 0x336472B9, 0xBE609BEA, 0xF55E226B, 0x99958DAC),
        (0x76441D41, 0x4DE82659, 0x2E8EF85E, 0xB256FACA, 0xC4FE2DE8),
    ),
    "r2": (
        (0x39B06E79, 0xC9BD25E8, 0xDBC6B498, 0x40B4389D, 0x86BBD7ED),
        (0x9B49BED3, 0x84B430FC, 0x6B8749CD, 0xEBFE5FE5, 0xD96E7393),
    ),
    "r3": (
        (0x8058763A, 0xF97D4E0E, 0x865A9762, 0x8A3D920D, 0x08995B2C),
        (0x01075307, 0xA2F1E037, 0x1186EEEA, 0x88DA9992, 0x168A5609),
    ),
}

# xemu's temporary default user settings: GMT/BST, English language, remaining
# bytes zero. Kept byte-for-byte for compatibility with that generator.
_DEFAULT_USER_SECTION_PREFIX = bytes.fromhex(
    "00000000474d54004253540000000000000000000a050002030500010000"
    "00000000000000000000c4ffffff"
)


class EepromError(ValueError):
    pass


@dataclass(frozen=True)
class ValidationResult:
    ok: bool
    revision: Optional[str] = None
    reason: str = ""


class _DeterministicRandom:
    def __init__(self, data: bytes):
        self._data = memoryview(data)
        self._offset = 0

    def __call__(self, n: int) -> bytes:
        end = self._offset + n
        if end > len(self._data):
            raise EepromError("deterministic random byte stream is too short")
        out = self._data[self._offset:end].tobytes()
        self._offset = end
        return out


def _random_source(random_bytes: Optional[Union[bytes, bytearray, Callable[[int], bytes]]]
                   ) -> Callable[[int], bytes]:
    if random_bytes is None:
        return secrets.token_bytes
    if callable(random_bytes):
        return random_bytes
    return _DeterministicRandom(bytes(random_bytes))


def _draw(rnd: Callable[[int], bytes], n: int, label: str) -> bytes:
    data = rnd(n)
    if not isinstance(data, (bytes, bytearray)):
        raise EepromError(f"random source for {label} did not return bytes")
    data = bytes(data)
    if len(data) != n:
        raise EepromError(f"random source for {label} returned {len(data)} bytes, expected {n}")
    return data


def _u32(x: int) -> int:
    return x & 0xFFFFFFFF


def _eeprom_crc(data: bytes) -> int:
    if len(data) % 4:
        raise EepromError("EEPROM checksum input length must be a multiple of four")
    high = 0
    low = 0
    for (value,) in struct.iter_unpack("<I", data):
        total = ((high << 32) | low) + value
        high = (total >> 32) & 0xFFFFFFFF
        low = (low + value) & 0xFFFFFFFF
    return _u32(~_u32(high + low))


def _sha1_compress(block: bytes, state: tuple[int, int, int, int, int]) -> tuple[int, int, int, int, int]:
    if len(block) != 64:
        raise EepromError("internal SHA1 block size error")
    words = list(struct.unpack(">16I", block))
    for i in range(16, 80):
        v = words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16]
        words.append(_u32((v << 1) | (v >> 31)))

    a, b, c, d, e = state
    for i, w in enumerate(words):
        if i < 20:
            f = (b & c) | ((~b) & d)
            k = 0x5A827999
        elif i < 40:
            f = b ^ c ^ d
            k = 0x6ED9EBA1
        elif i < 60:
            f = (b & c) | (b & d) | (c & d)
            k = 0x8F1BBCDC
        else:
            f = b ^ c ^ d
            k = 0xCA62C1D6
        temp = _u32(((a << 5) | (a >> 27)) + f + e + k + w)
        e = d
        d = c
        c = _u32((b << 30) | (b >> 2))
        b = a
        a = temp
    return tuple(_u32(x + y) for x, y in zip(state, (a, b, c, d, e)))  # type: ignore[return-value]


def _sha1_digest(data: bytes,
                 initial_state: tuple[int, int, int, int, int] = (
                     0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0),
                 initial_len_bytes: int = 0) -> bytes:
    state = initial_state
    total_len = initial_len_bytes + len(data)
    work = bytearray(data)
    work.append(0x80)
    while (initial_len_bytes + len(work)) % 64 != 56:
        work.append(0)
    work += struct.pack(">Q", total_len * 8)
    for offset in range(0, len(work), 64):
        state = _sha1_compress(bytes(work[offset:offset + 64]), state)
    return struct.pack(">5I", *state)


def _xbox_sha1_compute(revision: str, data: bytes) -> bytes:
    try:
        first_state, second_state = _REVISION_STATES[revision]
    except KeyError as exc:
        raise EepromError(f"unsupported EEPROM revision: {revision}") from exc
    first_digest = _sha1_digest(data, first_state, initial_len_bytes=64)
    return _sha1_digest(first_digest, second_state, initial_len_bytes=64)


def _rc4_crypt(key: bytes, data: bytes) -> bytes:
    s = list(range(256))
    j = 0
    key_len = len(key)
    if key_len == 0:
        raise EepromError("RC4 key must not be empty")
    for i in range(256):
        j = (j + s[i] + key[i % key_len]) & 0xFF
        s[i], s[j] = s[j], s[i]
    out = bytearray()
    i = 0
    j = 0
    for byte in data:
        i = (i + 1) & 0xFF
        j = (j + s[i]) & 0xFF
        s[i], s[j] = s[j], s[i]
        out.append(byte ^ s[(s[i] + s[j]) & 0xFF])
    return bytes(out)


def _default_user_section() -> bytearray:
    section = bytearray(156)
    section[:len(_DEFAULT_USER_SECTION_PREFIX)] = _DEFAULT_USER_SECTION_PREFIX
    struct.pack_into("<I", section, 0x2C, 1)
    return section


def generate_eeprom(revision: str = "r2",
                    random_bytes: Optional[Union[bytes, bytearray, Callable[[int], bytes]]] = None
                    ) -> bytes:
    """Return a fresh 256-byte EEPROM image.

    The generated profile uses the current xboxlle-tested firmware authentication
    revision by default: r2. It also keeps xemu's North America, NTSC-M,
    GMT/BST English user-setting defaults. Secret/identity fields are freshly
    generated unless a deterministic byte stream is supplied for tests.
    """
    if revision not in _REVISION_STATES:
        raise EepromError("revision must be one of: debug, r1, r2, r3")
    rnd = _random_source(random_bytes)
    # Draw and validate every random field before mutating the output image.
    # A buggy callback must not shrink bytearray slice assignments into a
    # malformed non-256-byte result.
    confounder = _draw(rnd, 8, "confounder")
    hdd_key = _draw(rnd, 16, "HDD key")
    online_key = _draw(rnd, 16, "online key")
    mac_raw = bytearray(_draw(rnd, 6, "MAC address"))
    mac_raw[0] = (mac_raw[0] | 0x02) & 0xFE
    mac = bytes(mac_raw)
    serial_raw = _draw(rnd, 12, "serial")
    serial = bytes(ord("0") + (b % 10) for b in serial_raw)

    image = bytearray(EEPROM_SIZE)
    security_plain = bytearray()
    security_plain += confounder
    security_plain += hdd_key
    security_plain += struct.pack("<I", _DEFAULT_REGION_NORTH_AMERICA)
    digest = _xbox_sha1_compute(revision, bytes(security_plain))
    seed = _xbox_sha1_compute(revision, digest)
    security_encrypted = _rc4_crypt(seed, bytes(security_plain))

    image[0:20] = digest
    image[_SECURITY_OFFSET:_SECURITY_OFFSET + _SECURITY_SIZE] = security_encrypted
    image[_SERIAL_OFFSET:_SERIAL_OFFSET + 12] = serial
    image[_SERIAL_OFFSET + 12:_SERIAL_OFFSET + 18] = mac
    image[72:88] = online_key
    struct.pack_into("<I", image, 88, _DEFAULT_VIDEO_NTSC_M)
    struct.pack_into("<I", image, 48, _eeprom_crc(bytes(image[_SERIAL_OFFSET:_SERIAL_OFFSET + _SERIAL_CHECKSUM_SIZE])))
    user_section = _default_user_section()
    image[_USER_SECTION_OFFSET:] = user_section
    struct.pack_into("<I", image, _USER_CHECKSUM_OFFSET, _eeprom_crc(bytes(user_section[:_USER_CHECKSUM_SIZE])))
    return bytes(image)


def _decrypt_security(image: bytes, revision: str) -> bytes:
    digest = image[0:20]
    seed = _xbox_sha1_compute(revision, digest)
    return _rc4_crypt(seed, image[_SECURITY_OFFSET:_SECURITY_OFFSET + _SECURITY_SIZE])


def read_eeprom_file(path: Path) -> bytes:
    fd = -1
    try:
        # Validate the opened object, not a separate path stat that can become
        # stale. Nonblocking open avoids waiting on a substituted POSIX FIFO.
        flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NONBLOCK", 0)
        fd = os.open(path, flags)
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise EepromError("EEPROM path is not a regular file")
        with os.fdopen(fd, "rb") as f:
            fd = -1
            data = f.read(EEPROM_SIZE + 1)
    except OSError as exc:
        raise EepromError(f"unable to read EEPROM image: {exc}") from exc
    finally:
        if fd >= 0:
            os.close(fd)
    if len(data) != EEPROM_SIZE:
        raise EepromError("EEPROM image must be exactly 256 bytes")
    return data


def _same_file_identity(a: os.stat_result, b: os.stat_result) -> bool:
    return (
        getattr(a, "st_dev", None),
        getattr(a, "st_ino", None),
    ) == (
        getattr(b, "st_dev", None),
        getattr(b, "st_ino", None),
    )


def validate_eeprom(data: bytes, revision: Optional[str] = None) -> ValidationResult:
    if len(data) != EEPROM_SIZE:
        return ValidationResult(False, reason="EEPROM image must be exactly 256 bytes")
    serial_region = data[_SERIAL_OFFSET:_SERIAL_OFFSET + _SERIAL_CHECKSUM_SIZE]
    expected_serial_crc = struct.unpack_from("<I", data, 48)[0]
    if _eeprom_crc(serial_region) != expected_serial_crc:
        return ValidationResult(False, reason="serial/settings checksum mismatch")
    user_region = data[_USER_SECTION_OFFSET:_USER_SECTION_OFFSET + _USER_CHECKSUM_SIZE]
    expected_user_crc = struct.unpack_from("<I", data, _USER_CHECKSUM_OFFSET)[0]
    if _eeprom_crc(user_region) != expected_user_crc:
        return ValidationResult(False, reason="user-section checksum mismatch")

    revisions: Iterable[str] = (revision,) if revision else ("debug", "r1", "r2", "r3")
    auth_mismatch = False
    for candidate in revisions:
        if candidate not in _REVISION_STATES:
            return ValidationResult(False, revision=revision, reason="unsupported EEPROM revision")
        plain = _decrypt_security(data, candidate)
        if _xbox_sha1_compute(candidate, plain) == data[0:20]:
            return ValidationResult(True, revision=candidate)
        auth_mismatch = True
    return ValidationResult(False, revision=revision,
                            reason="security-section authentication mismatch" if auth_mismatch
                            else "unsupported EEPROM revision")


def _default_output_path() -> Path:
    repo_root = Path(__file__).resolve().parents[1]
    return (repo_root.parent / "machine" / "eeprom.bin").resolve()


def write_new_eeprom(path: Path, revision: str) -> None:
    path = path.resolve()
    if path.exists():
        result = validate_eeprom(read_eeprom_file(path), revision)
        if result.ok:
            print(f"[eeprom] existing image validates as {result.revision}; left untouched: {path}")
            return
        raise EepromError(f"existing EEPROM image is not a valid {revision} image: {result.reason}")

    path.parent.mkdir(parents=True, exist_ok=True)
    fd = -1
    created_stat = None
    partial_created = False
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        created_stat = os.fstat(fd)
        partial_created = True
        with os.fdopen(fd, "wb") as f:
            fd = -1
            f.write(generate_eeprom(revision))
            f.flush()
            os.fsync(f.fileno())
    except Exception:
        if fd >= 0:
            os.close(fd)
        if partial_created and created_stat is not None:
            try:
                current_stat = path.stat()
            except FileNotFoundError:
                current_stat = None
            if current_stat is not None and _same_file_identity(created_stat, current_stat):
                try:
                    path.unlink()
                except FileNotFoundError:
                    pass
        raise
    print(f"[eeprom] created new local EEPROM image: {path}")
    print("[eeprom] keep this file private; no EEPROM fields or keys were printed")


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create or validate a local-only xboxlle EEPROM image.")
    parser.add_argument("path", nargs="?", type=Path, default=_default_output_path(),
                        help="output path; default is ../machine/eeprom.bin relative to the repo")
    parser.add_argument("--revision", choices=("r1", "r2", "r3", "debug"), default="r2",
                        help="EEPROM authentication revision; default: r2 for the current xboxlle-tested firmware profile")
    parser.add_argument("--validate-only", action="store_true",
                        help="validate an existing file for the selected revision without creating it")
    return parser.parse_args(argv)


def main(argv: Optional[list[str]] = None) -> int:
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    try:
        if args.validate_only:
            data = read_eeprom_file(args.path)
            result = validate_eeprom(data, args.revision)
            if not result.ok:
                raise EepromError(result.reason)
            print(f"[eeprom] image validates as {result.revision}; no fields printed: {args.path.resolve()}")
            return 0
        write_new_eeprom(args.path, args.revision)
        return 0
    except Exception as exc:
        print(f"[eeprom] error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
