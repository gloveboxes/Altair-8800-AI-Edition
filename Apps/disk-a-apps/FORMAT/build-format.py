#!/usr/bin/env python3
"""Build Intel-8080 FORMAT.ASM using z80asm without changing its source dialect."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

REG = {'m': '(hl)'}
PAIR = {'b': 'bc', 'd': 'de', 'h': 'hl', 'sp': 'sp', 'psw': 'af'}
FIXED = {'ret':'ret', 'nop':'nop', 'xchg':'ex de,hl', 'xthl':'ex (sp),hl',
         'pchl':'jp (hl)', 'sphl':'ld sp,hl', 'rlc':'rlca', 'rrc':'rrca',
         'ral':'rla', 'rar':'rra', 'stc':'scf', 'cmc':'ccf', 'cma':'cpl',
         'daa':'daa', 'di':'di', 'ei':'ei', 'hlt':'halt'}
ALU = {'add':'add a,', 'adc':'adc a,', 'sub':'sub ', 'sbb':'sbc a,',
       'ana':'and ', 'xra':'xor ', 'ora':'or ', 'cmp':'cp ',
       'adi':'add a,', 'aci':'adc a,', 'sui':'sub ', 'sbi':'sbc a,',
       'ani':'and ', 'xri':'xor ', 'ori':'or ', 'cpi':'cp '}
CONDS = {'nz','z','nc','c','po','pe','p','m'}


def translate(source):
    result = []
    for number, raw in enumerate(source.splitlines(), 1):
        line = raw.split(';', 1)[0].strip().lower()
        if not line:
            continue
        line = re.sub(r'\b([0-9][0-9a-f]*)h\b', lambda m: '0x' + m[1], line)
        if ':' in line:
            label, line = line.split(':', 1)
            result.append(label + ':')
            line = line.strip()
            if not line:
                continue
        if re.match(r'\w+\s+equ\s', line):
            name, _, value = line.split(None, 2)
            result.append(name + ': equ ' + value)
            continue
        parts = line.split(None, 1)
        op, args = parts[0], parts[1] if len(parts) > 1 else ''
        operands = [v.strip() for v in args.split(',')]
        reg = lambda v: REG.get(v, v)
        if op == 'end':
            continue
        if op in ('org', 'db', 'dw'):
            out = op + ' ' + args
        elif op == 'ds':
            out = 'defs ' + args
        elif op in ('mov', 'mvi'):
            out = 'ld ' + ','.join(reg(v) for v in operands)
        elif op == 'lxi':
            out = 'ld ' + PAIR[operands[0]] + ',' + operands[1]
        elif op in ('lda', 'lhld'):
            out = 'ld ' + ('a' if op == 'lda' else 'hl') + ',(' + args + ')'
        elif op in ('sta', 'shld'):
            out = 'ld (' + args + '),' + ('a' if op == 'sta' else 'hl')
        elif op in ('ldax', 'stax'):
            out = ('ld a,(' + PAIR[args] + ')' if op == 'ldax'
                   else 'ld (' + PAIR[args] + '),a')
        elif op in ('push', 'pop'):
            out = op + ' ' + PAIR[args]
        elif op in ('inx', 'dcx', 'inr', 'dcr'):
            out = ('inc ' if op in ('inx', 'inr') else 'dec ')
            out += PAIR[args] if op in ('inx', 'dcx') else reg(args)
        elif op == 'dad':
            out = 'add hl,' + PAIR[args]
        elif op in FIXED:
            out = FIXED[op]
        elif op in ALU:
            out = ALU[op] + reg(args)
        elif op == 'jmp':
            out = 'jp ' + args
        elif op == 'call':
            out = 'call ' + args
        elif op[0] in 'jcr' and op[1:] in CONDS:
            out = {'j':'jp ', 'c':'call ', 'r':'ret '}[op[0]] + op[1:]
            if op[0] != 'r':
                out += ',' + args
        else:
            raise ValueError('Unsupported instruction at line %d: %s' % (number, raw))
        result.append(out)
    return '\n'.join(result) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assembler', default='z80asm')
    parser.add_argument('--output', type=Path, default=Path(__file__).with_name('FORMAT.COM'))
    args = parser.parse_args()
    source = Path(__file__).with_name('FORMAT.ASM').read_text(encoding='ascii')
    with tempfile.TemporaryDirectory(prefix='format-asm-') as folder:
        assembly = Path(folder) / 'format-z80.asm'
        assembly.write_text(translate(source), encoding='ascii')
        subprocess.run([args.assembler, '-o', str(args.output.resolve()), str(assembly)], check=True)
    print('Built %s (%d bytes)' % (args.output, args.output.stat().st_size))


if __name__ == '__main__':
    main()
