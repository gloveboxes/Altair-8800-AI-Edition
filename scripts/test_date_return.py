#!/usr/bin/env python3
"""Check repeated DATE calls and CCP returns in real CP/M on temporary disks."""
from pathlib import Path
import re
import shutil
import tempfile
import unittest

import test_cpm_install as cpm
from test_cpm_install import ROOT, boot
from test_ls_return import WATCH, COUNT, assemble


class DateReturnTests(unittest.TestCase):
    def test_repeated_date_without_warm_boot(self):
        with tempfile.TemporaryDirectory(prefix='date-return-') as temporary:
            directory = Path(temporary)
            for drive, name in zip('abcd', ['cpm63k.dsk', 'bdsc-v1.60.dsk',
                                           'escape-posix.dsk', 'blank.dsk']):
                shutil.copyfile(ROOT / 'disks' / name, directory / f'{drive}.dsk')
            (directory / 'env.txt').write_text('')
            source = ROOT / 'Apps/disk-a-apps/DATE/DATE.ASM'
            programs = [('DATE', source.read_text()), ('WATCH', WATCH), ('COUNT', COUNT)]
            for name, text in programs:
                cpm.InstallTests.install(directory / 'a.dsk', assemble(directory, name, text))
            repetitions = 8
            commands = [b'watch\r'] + [b'date\r'] * repetitions + [b'count\r', b'dir\r']
            output = boot(directory, commands, a_prompts=len(commands) + 1)
            self.assertNotIn('clock service unavailable', output, output)
            dates = re.findall(r'\b[A-Z][a-z]{2} \d{2}/\d{2}/\d{4} \d{2}:\d{2}:\d{2}', output)
            self.assertEqual(len(dates), repetitions, output)
            self.assertEqual(re.findall(r'WBOOTS=(\d+)', output), ['0'], output)
            self.assertNotIn('Bdos Err', output)


if __name__ == '__main__':
    unittest.main()
