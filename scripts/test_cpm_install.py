#!/usr/bin/env python3
"""Exercise Altair sector headers and CP/M boot after real cpmtools installs.

Requires pwsh, cpmtools, and a built altair_local/build/altair-local.
All disk writes use temporary images.
"""
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


def boot(directory):
    args = [str(ROOT / 'altair_local/build/altair-local'), '--env-file', str(directory / 'env.txt')]
    for drive in 'abcd':
        args += [f'--drive-{drive}', str(directory / f'{drive}.dsk')]
    master, slave = pty.openpty()
    process = subprocess.Popen(args, cwd=ROOT, stdin=slave, stdout=slave, stderr=slave)
    os.close(slave)
    output = bytearray()
    started = time.monotonic()
    sent = False
    deadline = started + 20
    try:
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.1)
            if not sent and time.monotonic() - started >= 3:
                os.write(master, b'dir\rnewapp\rb:\ra:\rdir\r')
                sent = True
            if not ready:
                continue
            try:
                data = os.read(master, 65536)
            except OSError:
                break
            output.extend(data)
            if b'Bad Sector' in output:
                break
            if output.count(b'A>') >= 5 and b'B>' in output:
                return output.decode(errors='replace')
        raise AssertionError(f'CP/M failed to complete commands:\n{output.decode(errors="replace")}')
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)


class InstallTests(unittest.TestCase):
    def test_unused_sector_headers_and_boot(self):
        with tempfile.TemporaryDirectory(prefix='cpm-install-test-') as temporary:
            directory = Path(temporary)
            for drive, name in zip('abcd', ['cpm63k.dsk', 'bdsc-v1.60.dsk', 'escape-posix.dsk', 'blank.dsk']):
                (directory / f'{drive}.dsk').write_bytes(committed(name))
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
            # The pristine boot image contains these stale free-sector headers.
            stale = [offset for offset in range(2 * TRACK, 77 * TRACK, FRAME)
                     if before[offset] & 0x7f != offset // TRACK]
            self.assertTrue(stale, 'Fixture must contain unused sectors with stale headers')
            output = boot(directory)
            self.assertIn('NEWAPP', output.upper())
            self.assertNotIn('Bad Sector', output)


if __name__ == '__main__':
    unittest.main()
