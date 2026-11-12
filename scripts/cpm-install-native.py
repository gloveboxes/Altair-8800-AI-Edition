#!/usr/bin/env python3
"""Install and verify a user-0 file on a 512 KiB SBC CP/M disk."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from z80_container import CONTAINER

ROOT = Path(__file__).resolve().parents[1]
CPM = CONTAINER / "sbc" / "src" / "cpm"
if not (CPM / "build_images.py").is_file():
    raise SystemExit("Initialize the z80-cpm-container submodule: git submodule update --init --recursive")
sys.path.insert(0, str(CPM))
import build_images


def install(image: Path, source: Path, destination: str) -> None:
    match = re.fullmatch(r"0:([A-Za-z0-9_]{1,8})\.([A-Za-z0-9_]{1,3})", destination)
    if not match:
        raise ValueError(f"expected a user-0 8.3 filename, got {destination!r}")
    name = f"{match[1].upper()}.{match[2].upper()}"
    cpmcp = shutil.which("cpmcp")
    cpmrm = shutil.which("cpmrm")
    if not cpmcp or not cpmrm:
        raise RuntimeError("cpmtools cpmcp and cpmrm must be on PATH")
    disk = image.read_bytes()
    if len(disk) != build_images.DISK_IMAGE_BYTES:
        raise ValueError(f"{image} must be exactly 512 KiB")
    data = source.read_bytes()
    if source.suffix.lower() == ".bas":
        text = data.decode("utf-8").rstrip("\x1a")
        text = re.sub(r"\r\n|\r|\n", "\r\n", text)
        if text and not text.endswith("\r\n"):
            text += "\r\n"
        data = (text + "\x1a").encode("utf-8")
    name_field = match[1].upper().ljust(8) + match[2].upper().ljust(3)
    present = any(
        disk[offset] == 0 and
        bytes(value & 0x7f for value in disk[offset + 1:offset + 12]) == name_field.encode("ascii")
        for offset in range(build_images.SYSTEM_BYTES,
                            build_images.SYSTEM_BYTES + (build_images.DPB_DRM + 1) * 32, 32)
    )
    with tempfile.TemporaryDirectory(prefix="cpm-install-", dir=image.parent) as name_dir:
        directory = Path(name_dir)
        diskdefs = directory / "diskdefs"
        diskdefs.write_text(build_images.cpm_diskdefs(), encoding="ascii")
        staged = directory / "disk.img"
        staged.write_bytes(disk)
        staged_file = directory / name
        staged_file.write_bytes(data)
        environment = {**os.environ, "DISKDEFS": str(diskdefs)}
        if present:
            subprocess.run([cpmrm, "-f", "z80sbc", str(staged), f"0:{name}"],
                           cwd=directory, env=environment, check=True)
        subprocess.run([cpmcp, "-f", "z80sbc", str(staged), str(staged_file), f"0:{name}"],
                       cwd=directory, env=environment, check=True)
        verify = directory / "verify"
        subprocess.run([cpmcp, "-f", "z80sbc", str(staged), f"0:{name}", str(verify)],
                       cwd=directory, env=environment, check=True)
        recovered = verify.read_bytes()
        if recovered[:len(data)] != data or len(recovered) < len(data):
            raise RuntimeError(f"incomplete installation of {name} into {image}")
        if staged.read_bytes()[:build_images.SYSTEM_BYTES] != disk[:build_images.SYSTEM_BYTES]:
            raise RuntimeError("installation attempted to modify reserved system tracks")
        os.chmod(staged, image.stat().st_mode)
        os.replace(staged, image)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--host-file", type=Path, required=True)
    parser.add_argument("--cpm-file", required=True)
    args = parser.parse_args()
    install(args.image.resolve(), args.host_file.resolve(), args.cpm_file)


if __name__ == "__main__":
    main()
