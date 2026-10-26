#!/usr/bin/env python3
"""Run LS in real CP/M and count BIOS warm boots, using temporary images.

Requires the built altair-local emulator, native m80c/l80c (PATH or DCC_DIR),
PowerShell and cpmtools. Tests also cover repeated calls, filespecs, sorting,
empty results and restoration of the original drive.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

import test_cpm_install as cpm
from test_cpm_install import ROOT, boot


# Install a RAM-only hook on the BIOS warm-boot jump table entry. The stub
# increments byte 0050H, then jumps to the original BIOS implementation.
# Both stub and counter are outside the FCB/DMA and survive warm boot.
# Calling WATCH again resets the counter without chaining the hook to itself.
WATCH = """
        cseg
        xor a
        ld (0050h),a
        ld hl,(0001h)
        inc hl
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld a,d
        or a
        jr nz,patch
        ld a,e
        cp 0040h
        ret z
patch:  ld (hl),0
        dec hl
        ld (hl),0040h
        ld hl,0040h
        ld (hl),021h
        inc hl
        ld (hl),050h
        inc hl
        ld (hl),0
        inc hl
        ld (hl),034h
        inc hl
        ld (hl),0c3h
        inc hl
        ld (hl),e
        inc hl
        ld (hl),d
        ret
        end
"""

COUNT = """
        cseg
        ld a,(0050h)
        add a,'0'
        ld (digit),a
        ld de,msg
        ld c,9
        jp 5
msg:    db 'WBOOTS='
digit:  db '0',13,10,'$'
        end
"""

# Make BIOS SELDSK reject drive D in RAM. This proves LS queries the BIOS
# rather than accepting every letter in a fixed A-D range. Other drives
# still use the original SELDSK implementation. Hook RAM does not overlap
# WATCH's warm-boot hook or counter.
NOD = """
        cseg
        ld hl,(0001h)
        ld de,25
        add hl,de
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld (target),de
        ld (hl),0
        dec hl
        ld (hl),0028h
        ld hl,hook
        ld de,0028h
        ld bc,10
        ldir
        ret
hook:   ld hl,0
        ld a,c
        cp 3
        ret z
        db 0c3h
target: dw 0
        end
"""


def tool(name):
    directory = os.environ.get('DCC_DIR')
    if directory:
        return str(Path(directory) / (name + ('.exe' if os.name == 'nt' else '')))
    found = shutil.which(name)
    if not found:
        raise RuntimeError(f'{name} is required on PATH or in DCC_DIR')
    return found


def assemble(directory, name, source):
    (directory / f'{name}.MAC').write_text(source)
    for args in [[tool('m80c'), f'={name}.MAC', '/X', '/O', '/Z', '/L', '/C'],
                 [tool('l80c'), f'/P:100,{name},{name}/N/E/Y']]:
        result = subprocess.run(args, cwd=directory, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        if result.returncode:
            raise RuntimeError(f'{args[0]} failed:\n{result.stdout}')
    return directory / f'{name}.COM'


class LsReturnTests(unittest.TestCase):
    def test_bios_drive_validation(self):
        with tempfile.TemporaryDirectory(prefix='ls-bios-') as temporary:
            directory = Path(temporary)
            for drive, name in zip('abcd', ['cpm63k.dsk', 'bdsc-v1.60.dsk',
                                           'escape-posix.dsk', 'blank.dsk']):
                shutil.copyfile(ROOT / 'disks' / name, directory / f'{drive}.dsk')
            (directory / 'env.txt').write_text('')
            source = ROOT / 'Apps/disk-all-apps/LS/LS.ASM'
            ls = assemble(directory, 'LS', source.read_text())
            self.assertLessEqual(ls.stat().st_size, 4096)
            for name, text in [('WATCH', WATCH), ('COUNT', COUNT), ('NOD', NOD)]:
                cpm.InstallTests.install(directory / 'a.dsk', assemble(directory, name, text))
            for drive in 'ac':
                cpm.InstallTests.install(directory / f'{drive}.dsk', ls)

            # First accept D, then have the BIOS reject it. The following C:
            # and A: directory reads also check restoration of BIOS state.
            commands = [b'watch\r', b'ls D:\r', b'nod\r', b'ls D:\r',
                        b'ls -s D:*.COM\r', b'ls C: -s\r', b'count\r', b'dir\r']
            output = boot(directory, commands, a_prompts=len(commands) + 1)
            text = re.sub(r'\x1b\[[0-9;]*m', '', output).replace('\r', '')
            sections = text.split('A>')[1:-1]
            self.assertIn('remain on D:', sections[1])
            self.assertIn('LS: invalid drive: D:', sections[3])
            self.assertIn('LS: invalid drive: D:*.COM', sections[4])
            self.assertIn('remain on C:', sections[5])
            self.assertEqual(re.findall(r'WBOOTS=(\d+)', text), ['0'], text)
            self.assertNotIn('Bdos Err', text)

            # Reproduce the reported C>ls e: command, including option forms.
            commands = [b'watch\r', b'c:\r', b'ls e:\r', b'ls -s e:*.com\r',
                        b'ls p: -s\r', b'ls a:*.com\r', b'a:\r', b'count\r', b'dir\r']
            output = boot(directory, commands, a_prompts=5)
            text = re.sub(r'\x1b\[[0-9;]*m', '', output).replace('\r', '')
            self.assertIn('C>ls e:\nLS: invalid drive: E:', text)
            self.assertIn('LS: invalid drive: E:*.COM', text)
            self.assertIn('LS: invalid drive: P:', text)
            self.assertIn('remain on A:', text)
            self.assertEqual(re.findall(r'WBOOTS=(\d+)', text), ['0'], text)
            self.assertNotIn('Bdos Err', text)

    def test_help_and_invalid_arguments(self):
        with tempfile.TemporaryDirectory(prefix='ls-args-') as temporary:
            directory = Path(temporary)
            for drive, name in zip('abcd', ['cpm63k.dsk', 'bdsc-v1.60.dsk',
                                           'escape-posix.dsk', 'blank.dsk']):
                shutil.copyfile(ROOT / 'disks' / name, directory / f'{drive}.dsk')
            (directory / 'env.txt').write_text('')
            source = ROOT / 'Apps/disk-all-apps/LS/LS.ASM'
            ls = assemble(directory, 'LS', source.read_text())
            self.assertLessEqual(ls.stat().st_size, 4096)
            for name, text in [('WATCH', WATCH), ('COUNT', COUNT)]:
                cpm.InstallTests.install(directory / 'a.dsk', assemble(directory, name, text))
            cpm.InstallTests.install(directory / 'a.dsk', ls)
            cases = [
                ('ls -h', 'help'),
                ('ls -x', '-X'),
                ('ls -s -s', '-S'),
                ('ls -ss', '-SS'),
                ('ls FOO BAR', 'BAR'),
                ('ls -h -s', '-S'),
                ('ls ABCDEFGHI.COM', 'ABCDEFGHI.COM'),
                ('ls FILE.LONG', 'FILE.LONG'),
                ('ls C:FILE..COM', 'C:FILE..COM'),
                ('ls E:', 'drive:E:'),
                ('ls Q:', 'drive:Q:'),
                ('ls BAD/NAME', 'BAD/NAME'),
                ('ls A*B.COM', 'A*B.COM'),
                ('ls -s C:', 'listing'),
                ('ls C: -s', 'listing'),
                ('ls C:', 'listing'),
                ('ls -s', 'listing'),
                ('ls *.COM -s', 'listing'),
            ]
            for start in range(0, len(cases), 6):
                batch = cases[start:start + 6]
                commands = [b'watch\r'] + [(cmd + '\r').encode() for cmd, _ in batch]
                commands += [b'count\r', b'dir\r']
                output = boot(directory, commands, a_prompts=len(commands) + 1)
                text = re.sub(r'\x1b\[[0-9;]*m', '', output).replace('\r', '')
                self.assertEqual(re.findall(r'WBOOTS=(\d+)', text), ['0'], text)
                sections = text.split('A>')[2:2 + len(batch)]
                for (cmd, expected), section in zip(batch, sections):
                    with self.subTest(command=cmd):
                        if expected == 'help':
                            self.assertIn('Usage: LS [-S] [filespec]', section)
                            self.assertIn('-H  Show help.', section)
                            self.assertNotIn('File(s)', section)
                        elif expected == 'listing':
                            self.assertIn('File(s)', section)
                            self.assertNotIn('invalid argument', section)
                        else:
                            if expected.startswith('drive:'):
                                message = 'LS: invalid drive: ' + expected[6:]
                            else:
                                message = 'LS: invalid argument: ' + expected
                            self.assertIn(message, section)
                            self.assertIn('Use LS -H for help.', section)
                            self.assertNotIn('File(s)', section)

    def test_return_without_warm_boot(self):
        with tempfile.TemporaryDirectory(prefix='ls-return-') as temporary:
            directory = Path(temporary)
            for drive, name in zip('abcd', ['cpm63k.dsk', 'bdsc-v1.60.dsk',
                                           'escape-posix.dsk', 'blank.dsk']):
                shutil.copyfile(ROOT / 'disks' / name, directory / f'{drive}.dsk')
            (directory / 'env.txt').write_text('')
            source = ROOT / 'Apps/disk-all-apps/LS/LS.ASM'
            ls = assemble(directory, 'LS', source.read_text())
            self.assertLessEqual(ls.stat().st_size, 4096)
            for name, text in [('WATCH', WATCH), ('COUNT', COUNT),
                               ('WARM', '        cseg\n        jp 0000h\n        end\n')]:
                cpm.InstallTests.install(directory / 'a.dsk', assemble(directory, name, text))
            cpm.InstallTests.install(directory / 'a.dsk', ls)
            for name, size in [('TSTA.DAT', 128), ('TSTB.DAT', 4096), ('TSTC.DAT', 2048)]:
                fixture = directory / name
                fixture.write_bytes(bytes(size))
                cpm.InstallTests.install(directory / 'c.dsk', fixture)

            commands = [b'watch\r', b'warm\r', b'count\r',
                        b'watch\r', b'ls -s C:TST*.DAT\r', b'count\r',
                        b'ls C:TST*.DAT -s\r', b'count\r',
                        b'ls C:TST*.DAT\r', b'count\r',
                        b'ls C:NONE.*\r', b'count\r', b'dir\r']
            output = boot(directory, commands, a_prompts=len(commands) + 1)
            text = re.sub(r'\x1b\[[0-9;]*m', '', output).replace('\r', '')
            # The positive control proves the hook sees an actual warm boot.
            self.assertEqual(re.findall(r'WBOOTS=(\d+)', text), ['1', '0', '0', '0', '0'], text)
            sections = text.split('A>')[1:-1]
            for index in [4, 6]:
                listing = sections[index]
                self.assertLess(listing.index('TSTB'), listing.index('TSTA'), listing)
                self.assertLess(listing.index('TSTA'), listing.index('TSTC'), listing)
                self.assertIn('3 File(s)', listing)
                self.assertIn('remain on C:', listing)
            alphabetical = sections[8]
            self.assertLess(alphabetical.index('TSTA'), alphabetical.index('TSTB'))
            self.assertLess(alphabetical.index('TSTB'), alphabetical.index('TSTC'))
            self.assertIn('0 File(s)', sections[10])
            self.assertIn('LS       COM', sections[12])
            self.assertNotIn('Bad Sector', text)


if __name__ == '__main__':
    unittest.main()
