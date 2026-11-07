#!/usr/bin/env python3
"""Build isolated dcc/CP/M calculator checks and optional Z80 cycle benchmarks."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
APP = HERE.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=APP)
    parser.add_argument('--bench', action='store_true')
    parser.add_argument('--bench-only', action='store_true')
    args = parser.parse_args()
    dccmake = Path(shutil.which('dccmake') or Path(os.environ['DCC_DIR']) / 'dccmake')
    dcc = Path(os.environ.get('DCC_DIR', dccmake.parent))
    ntvcm = Path(shutil.which('ntvcm') or Path(os.environ['NTVCM_DIR']) / 'ntvcm')
    with tempfile.TemporaryDirectory(prefix='sheets-calc-') as temporary:
        root = Path(temporary)
        work = root / 'SHEETS'
        work.mkdir()
        (root / 'COMMON').mkdir()
        shutil.copy2(APP.parents[1] / 'COMMON/DXSVC.H', root / 'COMMON/DXSVC.H')
        for name in ('SHEETS.H', 'SHEETC.C', 'sheets_calc_c11.c'):
            shutil.copy2(args.source / name, work / name)

        def build(source):
            shutil.copy2(HERE / source, work / 'DRIVER.C')
            (work / 'dccmake.txt').write_text('\n'.join((
                'dcc-input=DRIVER.C,SHEETC.C', 'dcc-output=CHECKS',
                'dcc-floatio=false', 'dcc-flongio=false', 'dcc-peep=true',
                'dcc-stack-bytes=4096', 'dcc-build-dir=build',
                f'dcc-runtime={dcc}/DCCRTL.MAC',
                f'dcc-include-directory={dcc},../COMMON',
                f'dcc-tool={dcc}/dcc', f'dccpeep-tool={dcc}/dccpeep',
                f'dccrtlstrip-tool={dcc}/dccrtlstrip', f'ntvcm-tool={ntvcm}',
                f'm80-command={dcc}/m80c.com', f'l80-command={dcc}/l80c.com',
            )) + '\n')
            result = subprocess.run([str(dccmake)], cwd=work, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)

        if not args.bench_only:
            build('calc_checks.c')
            subprocess.run([str(ntvcm), '-c', 'build/CHECKS.COM'], cwd=work, check=True)
        if args.bench or args.bench_only:
            build('recalc_bench.c')
            for scenario, name in enumerate(('references', 'ranges', 'fanout', 'warm display', 'unrelated edit', 'cache collisions')):
                cycles = []
                for repetitions in (1, 11):
                    result = subprocess.run(
                        [str(ntvcm), '-c', '-p', 'build/CHECKS.COM', str(scenario), str(repetitions)],
                        cwd=work, check=True, capture_output=True, text=True)
                    match = re.search(r'Z80\s+cycles:\s+([\d,]+)', result.stdout)
                    if not match:
                        raise RuntimeError(result.stdout + result.stderr)
                    cycles.append(int(match.group(1).replace(',', '')))
                print(f'{name:16s} {(cycles[1] - cycles[0]) // 10:12,d} Z80 cycles/iteration')


if __name__ == '__main__':
    main()
