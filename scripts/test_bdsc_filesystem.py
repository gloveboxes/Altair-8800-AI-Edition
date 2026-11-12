#!/usr/bin/env python3
"""Check the shipped BDS C images and a real install with cpmtools."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
IMAGES = ("disks/bdsc-v1.60.dsk",
          "altair_mcp_server/pristine/bdsc-v1.60.dsk",
          "disks_cpm_3/bdsc-v1.60.dsk")


def pack(image, destination):
    data = image.read_bytes()
    packed = bytearray(32 * 128)  # padding track required by diskdefs
    for track in range(77):
        for sector in range(32):
            physical = sector if track < 6 else sector * 17 % 32
            offset = (track * 32 + physical) * 137 + (3 if track < 6 else 7)
            packed.extend(data[offset:offset + 128])
    destination.write_bytes(packed)


class BdscFilesystemTests(unittest.TestCase):
    def check_image(self, image, directory):
        packed = directory / 'packed.dsk'
        pack(image, packed)
        result = subprocess.run(['fsck.cpm', '-f', 'altair88', '-n', str(packed)],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        listing = subprocess.check_output(['cpmls', '-f', 'altair88', str(packed)],
                                          cwd=ROOT, text=True)
        self.assertEqual(listing.lower().split().count('tail.c'), 1)
        tail = directory / 'tail.c'
        subprocess.run(['cpmcp', '-f', 'altair88', '-T', 'raw', str(packed),
                        '0:TAIL.C', str(tail)], cwd=ROOT, check=True)
        content = tail.read_bytes()
        tail.unlink()
        return content

    def test_shipped_images_and_install(self):
        with tempfile.TemporaryDirectory(prefix='bdsc-fsck-') as temporary:
            directory = Path(temporary)
            for name in IMAGES:
                with self.subTest(image=name):
                    image = directory / 'image.dsk'
                    image.write_bytes((ROOT / name).read_bytes())
                    before = self.check_image(image, directory)
                    source = directory / 'TEST.COM'
                    source.write_bytes(b'\xc9' + bytes(2047))
                    subprocess.run(['pwsh', '-NoProfile', '-File',
                                    str(ROOT / 'scripts/cpm-install.ps1'),
                                    '-Image', str(image), '-HostFile', str(source)],
                                   cwd=ROOT, check=True, capture_output=True)
                    self.assertEqual(self.check_image(image, directory), before)


if __name__ == '__main__':
    unittest.main()
