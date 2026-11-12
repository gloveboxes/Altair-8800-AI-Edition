#!/usr/bin/env python3
"""Exercise 137-byte Altair disk installations using temporary images."""
import os
from pathlib import Path
import pty
import select
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
FRAME = 137
TRACK = 32 * FRAME


def committed(name):
    return subprocess.check_output(['git', 'show', f'HEAD:disks/{name}'], cwd=ROOT)


def boot(directory, commands=b'dir\rnewapp\rb:\ra:\rdir\r', a_prompts=5):
    raise unittest.SkipTest("137-byte disk execution requires an appropriate emulator backend")


@unittest.skip("137-byte disk execution requires an appropriate emulator backend")
class InstallTests(unittest.TestCase):
    def test_basic_crlf_and_mbasic_load(self):
        with tempfile.TemporaryDirectory(prefix='cpm-basic-test-') as temporary:
            directory = Path(temporary)
            for drive, name in zip('abcd', ['cpm63k.dsk', 'bdsc-v1.60.dsk', 'escape-posix.dsk', 'blank.dsk']):
                (directory / f'{drive}.dsk').write_bytes(committed(name))
            source = directory / 'TEXT.BAS'
            for data in [b'10 END\n20 END', b'10 END\r\n20 END\r\n',
                         b'10 END\r20 END\x1a', b'10 END\r\n20 END\n\x1a\x1a']:
                with self.subTest(data=data):
                    source.write_bytes(data)
                    self.install(directory / 'c.dsk', source)
                    self.assertEqual(source.read_bytes(), data)
                    installed = self.extract(directory, 'c', 'TEXT.BAS')
                    expected = b'10 END\r\n20 END\r\n\x1a'
                    self.assertEqual(installed[:len(expected)], expected)
            blink = ROOT / 'Apps/disk-c-apps/BLINK/BLINK.BAS'
            original = blink.read_bytes()
            self.install(directory / 'c.dsk', blink)
            self.assertEqual(blink.read_bytes(), original)
            installed = self.extract(directory, 'c', 'BLINK.BAS')
            self.assertNotIn(b'\n', installed.replace(b'\r\n', b''))
            self.assertIn(b'540 STOP\r\n\x1a', installed)
            output = boot(directory, [b'MBASIC\r', b'LOAD "C:BLINK.BAS"\r', b'LIST\r', b'SYSTEM\r'], a_prompts=2)
            self.assertIn('100 LS=0', output)
            self.assertIn('540 STOP', output)
            self.assertNotIn('Syntax error', output)
            self.assertNotIn('File not found', output)
            self.assertNotIn('Bad Sector', output)

    @staticmethod
    def install(image, source):
        subprocess.run(['pwsh', '-NoProfile', '-File', str(ROOT / 'scripts/cpm-install.ps1'),
                        '-Image', str(image), '-HostFile', str(source)], cwd=ROOT, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    @staticmethod
    def extract(directory, drive, name):
        image = (directory / f'{drive}.dsk').read_bytes()
        packed = bytearray()
        for track in range(77):
            for sector in range(32):
                physical = sector if track < 6 else sector * 17 % 32
                offset = (track * 32 + physical) * FRAME + (3 if track < 6 else 7)
                packed.extend(image[offset:offset + 128])
        packed_path = directory / 'packed.dsk'
        packed_path.write_bytes(packed)
        output = directory / 'extracted.bin'
        subprocess.run(['cpmcp', '-f', 'altair88', '-T', 'raw', str(packed_path),
                        '0:' + name, str(output)], cwd=ROOT, check=True)
        return output.read_bytes()

    def test_unused_sector_headers_and_boot(self):
        with tempfile.TemporaryDirectory(prefix='cpm-install-test-') as temporary:
            directory = Path(temporary)
            for drive, name in zip('abcd', ['cpm63k.dsk', 'bdsc-v1.60.dsk', 'escape-posix.dsk', 'blank.dsk']):
                (directory / f'{drive}.dsk').write_bytes(committed(name))
            # Recreate the stale track-0 header that triggered the original bug,
            # even when HEAD already contains a repaired image.
            damaged = bytearray((directory / 'a.dsk').read_bytes())
            damaged[(2 * 32 + 13) * FRAME] = 0x80
            (directory / 'a.dsk').write_bytes(damaged)
            before = (directory / 'a.dsk').read_bytes()
            # Exercise newly allocated zero-filled sectors with stale track-0
            # headers. RET makes this a runnable CP/M program.
            program = directory / 'NEWAPP.COM'
            program.write_bytes(b'\xc9' + bytes(8191))
            for drive in 'abcd':
                image = directory / f'{drive}.dsk'
                original = image.read_bytes()
                subprocess.run(['pwsh', '-NoProfile', '-File', str(ROOT / 'scripts/cpm-install.ps1'),
                                '-Image', str(image), '-HostFile', str(program)], cwd=ROOT, check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                result = image.read_bytes()
                self.assertEqual(result[:2 * TRACK], original[:2 * TRACK])
                self.assertEqual(result[77 * TRACK:], original[77 * TRACK:])
                for track in range(2, 77):
                    for physical in range(32):
                        offset = track * TRACK + physical * FRAME
                        frame = result[offset:offset + FRAME]
                        self.assertEqual(frame[0] & 0x7f, track)
                        if track < 6:
                            self.assertEqual(frame[131], 255)
                            self.assertEqual(frame[132], sum(frame[3:131]) & 255)
                        else:
                            self.assertEqual(frame[1], physical * 17 % 32)
                            self.assertEqual(frame[135:], b'\xff\x00')
                            self.assertEqual(frame[4], (sum(frame[7:135]) + sum(frame[i] for i in [2, 3, 5, 6])) & 255)
            # The fixture must exercise the stale address field.
            stale = [offset for offset in range(2 * TRACK, 77 * TRACK, FRAME)
                     if before[offset] & 0x7f != offset // TRACK]
            self.assertTrue(stale, 'Fixture must contain unused sectors with stale headers')
            output = boot(directory)
            self.assertIn('NEWAPP', output.upper())
            self.assertNotIn('Bad Sector', output)


if __name__ == '__main__':
    unittest.main()
