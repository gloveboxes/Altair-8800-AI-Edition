#!/usr/bin/env python3
"""Verify build-all installs into the separate 512 KiB disks."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from z80_container import CONTAINER
from test_cpm_install_native import CONTAINER_TOOLS, read_disk

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("pwsh") and CONTAINER_TOOLS,
                     "PowerShell and Apple container required")
class BuildAllNativeTests(unittest.TestCase):
    def test_default_installs_all_disks_without_touching_altair_disk(self):
        with tempfile.TemporaryDirectory(prefix="build native ") as name:
            root = Path(name)
            apps = root / "Apps"
            for bucket in "abcd":
                (apps / f"disk-{bucket}-apps").mkdir(parents=True)
            (apps / "disk-all-apps").mkdir(parents=True)
            (apps / "disk-all-apps/HELLO.BAS").write_bytes(b"10 PRINT 1\n")
            scripts = root / "scripts"
            scripts.mkdir()
            shutil.copyfile(ROOT / "Apps/build-all.ps1", apps / "build-all.ps1")
            shutil.copyfile(ROOT / "scripts/cpm-install-native.py", scripts / "cpm-install-native.py")
            shutil.copyfile(ROOT / "scripts/z80_container.py", scripts / "z80_container.py")
            source = CONTAINER / "sbc/src"
            target = root / "external/z80-cpm-container"
            target.parent.mkdir(parents=True)
            target.symlink_to(CONTAINER, target_is_directory=True)
            disks = root / "disks/512kb"
            disks.mkdir(parents=True)
            for path in (ROOT / "disks/512kb").glob("*.img"):
                shutil.copyfile(path, disks / path.name)
            old_disk = root / "disks/cpm63k.dsk"
            old_disk.write_bytes(b"untouched")
            for filename in ("bdsc-v1.60.dsk", "escape-posix.dsk", "blank.dsk"):
                (root / "disks" / filename).write_bytes(b"altair")
            clones = root / "clones"
            clones.mkdir()
            subprocess.run(
                ["pwsh", "-NoLogo", "-NoProfile", "-File", str(apps / "build-all.ps1"),
                 "-DiskFormat", "Altair", "-CloneOnly", "-Clone", str(clones)],
                cwd=root, check=True, capture_output=True, text=True)
            self.assertEqual((clones / "cpm63k.dsk").read_bytes(), b"untouched")
            self.assertEqual(len(list(clones.glob("*.dsk"))), 4)
            subprocess.run(["pwsh", "-NoLogo", "-NoProfile", "-File", str(apps / "build-all.ps1"),
                            "-c", str(clones)],
                           cwd=root, check=True, capture_output=True, text=True)
            self.assertEqual(old_disk.read_bytes(), b"untouched")
            sys_path = str(source / "cpm")
            import sys
            sys.path.insert(0, sys_path)
            try:
                import build_images
                (root / "diskdefs").write_text(build_images.cpm_diskdefs(), encoding="ascii")
            finally:
                sys.path.remove(sys_path)
            for image in disks.glob("*.img"):
                self.assertTrue(read_disk(root, image, "HELLO.BAS").startswith(b"10 PRINT 1\r\n\x1a"))
                self.assertEqual((clones / image.name).read_bytes(), image.read_bytes())


if __name__ == "__main__":
    unittest.main()
