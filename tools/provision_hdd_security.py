#!/usr/bin/env python3
"""Provision a virtual HDD from an authenticated, owner-supplied EEPROM.

No physical disk access. No credential discovery from guest execution.
Protocol reference: XboxDev/cromwell drivers/ide/BootIde.c,
CalculateDrivePassword / DriveSecurityChange. Uses Python's standard HMAC,
and the existing separately attributed LGPL EEPROM reader/decryptor.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import os
from pathlib import Path
import secrets

from create_eeprom import EepromError, _decrypt_security, read_eeprom_file, validate_eeprom

# Must match the virtual drive's IDENTIFY strings (not the owner's physical HDD).
MODEL = b"ST310211A"
SERIAL = b"XLLE000000000000000"


def derive_user_password(eeprom: bytes) -> bytes:
    result = validate_eeprom(eeprom)
    if not result.ok:
        raise EepromError(f"EEPROM validation failed: {result.reason}")
    plain = _decrypt_security(eeprom, result.revision)
    # Unswapped model followed by serial, with ATA trailing space padding removed.
    return hmac.new(plain[8:24], MODEL + SERIAL, hashlib.sha1).digest() + bytes(12)


def provision(eeprom_path: Path, output: Path) -> None:
    if output.suffix != ".key":
        raise ValueError("output must use the repository-excluded .key suffix")
    user = derive_user_password(read_eeprom_file(eeprom_path))
    # A new virtual drive has its own provisioned master credential. It is not
    # claimed to reproduce a physical manufacturer's master password.
    payload = user + secrets.token_bytes(32)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0)
    fd = os.open(output, flags, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eeprom", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        provision(args.eeprom, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"[hdd-security] {error}\n")
    print("[hdd-security] provisioned virtual drive; credentials kept in private .key file")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
