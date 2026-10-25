#!/usr/bin/env python3
"""Run FORMAT.COM against the SBC's independent Z80/BIOS execution harness."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=Path, required=True, help='Z80ROMlessSBC checkout')
args, remaining = parser.parse_known_args()
sys.path.insert(0, str(args.repo.resolve() / 'src/common/tests'))
import cpm_boot_z80_test as harness
import build_images

PROGRAM = Path(__file__).with_name('FORMAT.COM').read_bytes()


def wait_for(t, suffix, start):
    for _ in range(4000):
        t.machine.ticks_to_stop = 100000
        t.machine.run()
        if not t.input and bytes(t.output[start:]).endswith(suffix):
            return bytes(t.output[start:])
    raise AssertionError('No prompt: ' + repr(bytes(t.output[-500:])))


def send(t, text, suffix=b'COMMAND: '):
    start = len(t.output)
    t.input.extend(text)
    return wait_for(t, suffix, start)


def launch(t, tracks=128):
    t.until_prompt()
    if tracks == 77:
        old = struct.pack('<HBBBHHBBHH', 32, 4, 15, 0, 251, 63, 0x80, 0, 0, 2)
        new = struct.pack('<HBBBHHBBHH', 32, 4, 15, 0, 149, 63, 0xc0, 0, 0, 2)
        memory = bytes(t.machine.memory)
        position = memory.index(old, 0xfd00)
        t.machine.memory[position:position + len(old)] = new
    with tempfile.TemporaryDirectory(prefix='format-install-', dir='/tmp') as folder:
        folder = Path(folder)
        (folder / 'diskdefs').write_text(build_images.cpm_diskdefs())
        disk = folder / 'drive.img'; disk.write_bytes(t.disks[0])
        subprocess.run(['cpmrm', '-f', 'z80sbc', str(disk), '0:FORMAT.COM'],
                       cwd=folder, check=True)
        subprocess.run(['cpmcp', '-f', 'z80sbc', str(disk),
                        str(Path(__file__).with_name('FORMAT.COM')), '0:FORMAT.COM'],
                       cwd=folder, check=True)
        t.disks[0][:] = disk.read_bytes()
    send(t, b'FORMAT\r', b'DISK DRIVE (A-P)? ')
    return send(t, b'D\r')


class FormatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        harness.CpmBootExecution.setUpClass()

    def harness(self):
        t = harness.CpmBootExecution()
        t.setUp()
        return t

    def test_full_and_read_only_for_both_geometries(self):
        for tracks in (77, 128):
            with self.subTest(tracks=tracks):
                t = self.harness()
                geometry = launch(t, tracks)
                self.assertIn(('TRACKS PER DISK:   %d' % tracks).encode(), geometry)
                before = [bytes(disk) for disk in t.disks]
                send(t, b'FULL\r', b'TYPE C TO CONTINUE, A TO ABORT WITHOUT LOSS.\n\n\r')
                result = send(t, b'C')
                self.assertIn(b'NO ERRORS FOUND', result)
                self.assertNotIn(b'ERROR OCCURRED', result)
                self.assertEqual(len(t.writes), tracks * 32)
                self.assertEqual({(drive, kind) for drive, _, kind, _ in t.writes}, {(3, 1)})
                self.assertEqual([lba for _, lba, _, _ in t.writes], list(range(tracks * 32)))
                self.assertEqual(bytes(t.disks[3][:tracks * 4096]), b'\xe5' * (tracks * 4096))
                self.assertEqual(bytes(t.disks[3][tracks * 4096:]), before[3][tracks * 4096:])
                self.assertEqual([bytes(disk) for disk in t.disks[:3]], before[:3])
                writes, reads = len(t.writes), t.reads
                result = send(t, b'TEST\r')
                self.assertIn(b'TEST RUN COMPLETE', result)
                self.assertNotIn(b'BAD SECTOR FOUND', result)
                self.assertEqual(t.reads - reads, tracks * 32)
                self.assertEqual(len(t.writes), writes)
                self.assertIn(b'A>', send(t, b'STOP\r', b'A>'))

    def test_abort_does_not_write(self):
        t = self.harness(); launch(t)
        before = [bytes(disk) for disk in t.disks]
        send(t, b'FULL\r', b'TYPE C TO CONTINUE, A TO ABORT WITHOUT LOSS.\n\n\r')
        send(t, b'A')
        self.assertEqual(t.writes, [])
        self.assertEqual([bytes(disk) for disk in t.disks], before)

    def test_io_failure_is_reported(self):
        t = self.harness(); launch(t)
        original = t.write_port
        def write_port(address, value):
            original(address, value)
            if address & 0x17 == 0x10 and value == 1 and t.drive == 3:
                t.record.clear()
                t.disk_error = True
        t.machine.set_output_callback(write_port)
        result = send(t, b'TEST\r')
        self.assertIn(b'BAD SECTOR FOUND AT TRACK', result)
        self.assertEqual(t.writes, [])

    def test_cpm_mac_assembles_same_binary(self):
        # Verify the delivered Intel source with the real CP/M MAC + LOAD,
        # independently of the host dialect translator.
        t = self.harness()
        with tempfile.TemporaryDirectory(prefix='format-mac-', dir='/tmp') as folder:
            folder = Path(folder)
            (folder / 'diskdefs').write_text(build_images.cpm_diskdefs())
            disk = folder / 'drive.img'; disk.write_bytes(t.disks[3])
            subprocess.run(['cpmcp', '-f', 'z80sbc', str(disk),
                            str(Path(__file__).with_name('FORMAT.ASM')), '0:FORMAT.ASM'],
                           cwd=folder, check=True)
            t.disks[3][:] = disk.read_bytes()
            t.until_prompt()
            output = send(t, b'B:MAC D:FORMAT\r', b'A>')
            self.assertNotIn(b'MAC?', output)
            self.assertIn(b'END OF ASSEMBLY', output)
            output = send(t, b'B:LOAD D:FORMAT\r', b'A>')
            self.assertNotIn(b'LOAD?', output)

            disk.write_bytes(t.disks[3])
            compiled = folder / 'format.com'
            subprocess.run(['cpmcp', '-f', 'z80sbc', str(disk), '0:FORMAT.COM', str(compiled)],
                           cwd=folder, check=True)
            actual = compiled.read_bytes()
            self.assertEqual(actual[:len(PROGRAM)], PROGRAM)
            self.assertTrue(all(value == 0 for value in actual[len(PROGRAM):]))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *remaining], verbosity=2)
