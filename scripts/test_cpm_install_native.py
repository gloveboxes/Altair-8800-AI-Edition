#!/usr/bin/env python3
"""Verify native disk installation without touching repository disks."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from z80_container import CONTAINER

ROOT = Path(__file__).resolve().parents[1]
SBC = CONTAINER / "sbc"
sys.path.insert(0, str(SBC / "src/cpm"))
import build_images


HOST_TOOLS = bool(shutil.which("cpmcp") and shutil.which("cpmrm"))
CONTAINER_TOOLS = sys.platform == "darwin" and bool(shutil.which("container"))


def read_disk(directory: Path, image: Path, name: str) -> bytes:
    output = directory / "readback"
    output.unlink(missing_ok=True)
    arguments = ["-f", "z80sbc", str(image.relative_to(directory)), f"0:{name}", "readback"]
    if HOST_TOOLS:
        command = ["cpmcp", *arguments]
    else:
        command = [
            "container", "run", "--rm",
            "--mount", f"type=bind,source={directory},target=/work",
            "--workdir", "/work", "--env", "DISKDEFS=/work/diskdefs",
            "--entrypoint", "cpmcp", "z80-cpm:local", *arguments,
        ]
    subprocess.run(command, cwd=directory,
                   env={**os.environ, "DISKDEFS": str(directory / "diskdefs")}, check=True)
    return output.read_bytes()


@unittest.skipUnless(HOST_TOOLS or CONTAINER_TOOLS, "cpmtools or Apple container required")
class NativeInstallTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="native disk test ")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.image = self.directory / "drive.img"
        shutil.copyfile(ROOT / "disks/512kb/drive_d_blank.img", self.image)
        self.diskdefs = self.directory / "diskdefs"
        self.diskdefs.write_text(build_images.cpm_diskdefs(), encoding="ascii")

    def install(self, source: Path, destination: str, success=True):
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/cpm-install-native.py"),
             "--image", str(self.image), "--host-file", str(source),
             "--cpm-file", destination,
             *(["--container-worker"] if not CONTAINER_TOOLS else [])],
            capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)

    def read(self, name: str) -> bytes:
        return read_disk(self.directory, self.image, name)

    def test_install_replace_and_preserve_system_tracks(self):
        source = self.directory / "TEST.COM"
        original_tracks = self.image.read_bytes()[:build_images.SYSTEM_BYTES]
        for data in (b"first version" * 50, b"replacement"):
            source.write_bytes(data)
            self.install(source, "0:TEST.COM")
            self.assertTrue(self.read("TEST.COM").startswith(data))
        self.assertEqual(self.image.read_bytes()[:build_images.SYSTEM_BYTES], original_tracks)
        self.assertEqual(self.image.stat().st_size, 524288)

    def test_basic_conversion(self):
        source = self.directory / "TEST.BAS"
        source.write_bytes(b'10 PRINT "HELLO"\n20 END\r\n')
        self.install(source, "0:TEST.BAS")
        self.assertTrue(self.read("TEST.BAS").startswith(b'10 PRINT "HELLO"\r\n20 END\r\n\x1a'))

    def test_invalid_disk_or_filename_is_not_changed(self):
        source = self.directory / "TEST.COM"
        source.write_bytes(b"content")
        original = self.image.read_bytes()
        self.install(source, "1:TEST.COM", success=False)
        self.assertEqual(self.image.read_bytes(), original)
        self.image.write_bytes(b"not a disk")
        self.install(source, "0:TEST.COM", success=False)
        self.assertEqual(self.image.read_bytes(), b"not a disk")

    def test_full_disk_does_not_replace_original(self):
        source = self.directory / "HUGE.COM"
        source.write_bytes(b"x" * 524288)
        original = self.image.read_bytes()
        self.install(source, "0:HUGE.COM", success=False)
        self.assertEqual(self.image.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
