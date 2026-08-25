#!/usr/bin/env python3
"""Assemble the authoritative 64K Burcon BIOS and update its CP/M disk."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = REPO_ROOT / "disks" / "cpm64_bios.asm"
DEFAULT_DISK = REPO_ROOT / "disks" / "cpm63k.dsk"
DEFAULT_MIRRORS = (
    REPO_ROOT / "disk_archive" / "cpm63k.dsk",
    REPO_ROOT / "altair_mcp_server" / "pristine" / "cpm63k.dsk",
)

SECTOR_BYTES = 137
SECTORS_PER_TRACK = 32
TRACKS = 77
PAYLOAD_OFFSET = 3
PAYLOAD_BYTES = 128
STOP_OFFSET = 131
CHECKSUM_OFFSET = 132

BIOS_BASE = 0xF900
BIOS_BYTES = 0x0700
TRACK_ONE_LOAD_BASE = 0xF180
BIOS_FIRST_SECTOR = SECTORS_PER_TRACK + (
    (BIOS_BASE - TRACK_ONE_LOAD_BASE) // PAYLOAD_BYTES
)
BIOS_RECORDS = BIOS_BYTES // PAYLOAD_BYTES
MINIMUM_DISK_BYTES = TRACKS * SECTORS_PER_TRACK * SECTOR_BYTES
SYSTEM_TRACK_BYTES = 2 * SECTORS_PER_TRACK * SECTOR_BYTES


def parse_intel_hex(path: Path) -> dict[int, int]:
    """Return initialized absolute bytes from an Intel HEX file."""
    initialized: dict[int, int] = {}
    upper_address = 0
    saw_eof = False

    for line_number, text in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        if not text.startswith(":"):
            raise ValueError(f"{path}:{line_number}: invalid Intel HEX record")
        try:
            record = bytes.fromhex(text[1:])
        except ValueError as error:
            raise ValueError(f"{path}:{line_number}: invalid hexadecimal data") from error
        if len(record) < 5 or len(record) != record[0] + 5:
            raise ValueError(f"{path}:{line_number}: invalid record length")
        if sum(record) & 0xFF:
            raise ValueError(f"{path}:{line_number}: invalid record checksum")

        count = record[0]
        address = int.from_bytes(record[1:3], "big")
        record_type = record[3]
        data = record[4 : 4 + count]
        if record_type == 0:
            absolute = upper_address + address
            for offset, value in enumerate(data):
                initialized[absolute + offset] = value
        elif record_type == 1:
            saw_eof = True
        elif record_type == 2:
            if count != 2:
                raise ValueError(f"{path}:{line_number}: invalid segment address")
            upper_address = int.from_bytes(data, "big") << 4
        elif record_type == 4:
            if count != 2:
                raise ValueError(f"{path}:{line_number}: invalid linear address")
            upper_address = int.from_bytes(data, "big") << 16
        else:
            raise ValueError(f"{path}:{line_number}: unsupported record type {record_type}")

    if not saw_eof:
        raise ValueError(f"{path}: missing Intel HEX end-of-file record")
    return initialized


def assemble_bios(source: Path, zmac: str) -> dict[int, int]:
    with tempfile.TemporaryDirectory(prefix="cpm64-bios-") as directory:
        output_dir = Path(directory)
        subprocess.run(
            [zmac, "-8", "--od", str(output_dir), str(source)],
            check=True,
        )
        hex_path = output_dir / f"{source.stem}.hex"
        if not hex_path.is_file():
            raise FileNotFoundError(f"zmac did not create {hex_path.name}")
        return parse_intel_hex(hex_path)


def make_bios(initialized: dict[int, int]) -> bytes:
    bios = bytearray(BIOS_BYTES)
    for address, value in initialized.items():
        if not BIOS_BASE <= address < BIOS_BASE + BIOS_BYTES:
            raise ValueError(f"initialized byte outside BIOS region: 0x{address:04x}")
        bios[address - BIOS_BASE] = value
    if not initialized:
        raise ValueError("assembled BIOS contains no initialized bytes")
    return bytes(bios)


def update_disk(disk: bytes, bios: bytes) -> bytes:
    if len(disk) < MINIMUM_DISK_BYTES:
        raise ValueError(f"disk is too small: {len(disk)} bytes")
    if len(bios) != BIOS_BYTES:
        raise ValueError(f"BIOS must be exactly {BIOS_BYTES} bytes")

    updated = bytearray(disk)
    for record in range(BIOS_RECORDS):
        sector = BIOS_FIRST_SECTOR + record
        frame = sector * SECTOR_BYTES
        if updated[frame] & 0x7F != 1:
            raise ValueError(f"BIOS sector {sector} is not on track 1")
        payload = bios[record * PAYLOAD_BYTES : (record + 1) * PAYLOAD_BYTES]
        updated[frame + PAYLOAD_OFFSET : frame + PAYLOAD_OFFSET + PAYLOAD_BYTES] = payload
        updated[frame + STOP_OFFSET] = 0xFF
        updated[frame + CHECKSUM_OFFSET] = sum(payload) & 0xFF
    return bytes(updated)


def sync_system_tracks(disk: bytes, system_disk: bytes) -> bytes:
    if len(disk) < SYSTEM_TRACK_BYTES or len(system_disk) < SYSTEM_TRACK_BYTES:
        raise ValueError("disk is too small for two CP/M system tracks")
    return system_disk[:SYSTEM_TRACK_BYTES] + disk[SYSTEM_TRACK_BYTES:]


def resolve_zmac(requested: str | None) -> str:
    candidate = requested or os.environ.get("ZMAC") or shutil.which("zmac")
    if not candidate:
        raise FileNotFoundError(
            "zmac is required; install it from https://github.com/gp48k/zmac "
            "or pass --zmac"
        )
    return candidate


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as temporary:
        temporary.write(data)
        temporary_path = Path(temporary.name)
    temporary_path.chmod(stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o644)
    temporary_path.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--disk", type=Path, default=DEFAULT_DISK)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--zmac", help="Path to the zmac executable")
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify that the disk already matches the assembled source",
    )
    parser.add_argument(
        "--no-sync-mirrors",
        action="store_true",
        help="do not synchronize disk_archive and MCP pristine boot tracks",
    )
    args = parser.parse_args()

    try:
        initialized = assemble_bios(args.source, resolve_zmac(args.zmac))
        bios = make_bios(initialized)
        original = args.disk.read_bytes()
        updated = update_disk(original, bios)
        output = args.output or args.disk
        sync_mirrors = (
            not args.no_sync_mirrors
            and args.disk.resolve() == DEFAULT_DISK.resolve()
            and args.output is None
        )

        if args.check:
            if updated != original:
                print(f"{args.disk}: BIOS does not match {args.source}", file=sys.stderr)
                return 1
            if sync_mirrors:
                for mirror in DEFAULT_MIRRORS:
                    if sync_system_tracks(mirror.read_bytes(), original) != mirror.read_bytes():
                        print(f"{mirror}: system tracks do not match {args.disk}", file=sys.stderr)
                        return 1
            print(f"PASS: {args.disk} matches {args.source}")
            return 0

        write_atomic(output, updated)
        if sync_mirrors:
            for mirror in DEFAULT_MIRRORS:
                write_atomic(mirror, sync_system_tracks(mirror.read_bytes(), updated))
        print(
            f"wrote {output} from {len(initialized)} initialized BIOS bytes "
            f"({BIOS_RECORDS} sectors)"
        )
        if sync_mirrors:
            print(f"synchronized {len(DEFAULT_MIRRORS)} pristine system images")
        return 0
    except (FileNotFoundError, OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
