#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../../..');
assert(process.argv.length === 4 || process.argv.length === 5,
    'Usage: node z80_client_regression.mjs BUILD_DIR DCC_DIR [BASELINE_CLIENT.C]');
const work = path.resolve(process.argv[2]);
const dcc = path.resolve(process.argv[3]);
assert(work !== repo && !work.startsWith(repo + path.sep), 'BUILD_DIR must be outside the checkout');
fs.mkdirSync(work, { recursive: true });
const log = path.join(work, 'z80-client.log');
fs.writeFileSync(log, '');
function run(command, args, cwd = work) {
    const result = spawnSync(command, args, {
        cwd, encoding: 'utf8', timeout: 180000, maxBuffer: 4 * 1024 * 1024,
        env: { ...process.env, PATH: dcc + path.delimiter + process.env.PATH },
    });
    const output = (result.stdout ?? '') + (result.stderr ?? '');
    fs.appendFileSync(log, `$ ${command} ${args.join(' ')}\n${output}\n`);
    assert(!result.error, String(result.error));
    assert.equal(result.status, 0, output);
    return output;
}
const core = path.join(repo, 'altair8800');
run(process.env.CC ?? 'cc', ['-I' + core, '-c', path.join(core, 'memory.c'),
    '-o', path.join(work, 'memory.o')]);
run(process.env.CXX ?? 'c++', ['-std=c++17', '-O2', '-DALTAIR_X80_HOST_TEST=1',
    '-I' + core, '-I' + path.join(core, 'x80_compat'),
    path.join(here, 'z80_client_fixture.cpp'), path.join(core, 'cpu_x80_adapter.cpp'),
    path.join(core, 'x80.cxx'), path.join(work, 'memory.o'),
    '-o', path.join(work, 'z80-client-fixture')]);
const results = new Map();
const client = path.join(repo, 'Apps/COMMON/DXSVC.C');
const source = fs.readFileSync(client, 'utf8');
const blocks = source.match(/^#asm\r?\n[\s\S]*?^#endasm\r?$/gm);
assert.equal(blocks?.length, 1, 'Expected one embedded transport assembly block');
const reference = path.join(work, 'DXREF.C');
fs.writeFileSync(reference, source.replace(blocks[0], ''));
for (const [name, inputs] of [
    ...(process.argv[4] ? [['BEFORE', [path.resolve(process.argv[4])]]] : []),
    ['CREF', [reference, path.join(here, 'SVCCORE.C')]],
    ['ZCORE', [client]],
]) {
    run(path.join(dcc, 'dccmake'), [
        path.join(here, 'SVCBEN.C'), ...inputs,
        `dcc-output=${name}`, `dcc-build-dir=${path.join(work, name)}`,
        `dcc-runtime=${path.join(dcc, 'DCCRTL.MAC')}`,
        `dcc-include-directory=${dcc},${path.join(repo, 'Apps/COMMON')}`,
        'dcc-peep=true',
        `m80-command=${path.join(dcc, 'm80c.com')}`,
        `l80-command=${path.join(dcc, 'l80c.com')}`,
    ]);
    const output = run(path.join(work, 'z80-client-fixture'), [path.join(work, name, name)]);
    const match = output.match(/PASS bytes=(\d+) polling=(\d+) transfer=(\d+)/);
    assert(match, output);
    const timerLine = output.match(/^TIMERS (.+)$/m);
    assert(timerLine, output);
    const timers = Object.fromEntries([...timerLine[1].matchAll(/(\w+)=(\d+)/g)]
        .map(([, name, value]) => [name, Number(value)]));
    console.log(`${name}: ${match[0]}`);
    console.log(`${name}: ${timerLine[0]}`);
    results.set(name, { core: match.slice(1).map(Number), timers });
}
for (let i = 0; i < 3; ++i)
    assert(results.get('ZCORE').core[i] < results.get('CREF').core[i],
        `Assembly regressed ${['size', 'polling', 'transfer'][i]}`);
for (const [name, cycles] of Object.entries(results.get('ZCORE').timers)) {
    assert(cycles < results.get('CREF').timers[name], `Assembly regressed ${name}`);
    if (results.has('BEFORE'))
        assert(cycles <= results.get('BEFORE').timers[name], `Timer change regressed ${name}`);
}
if (results.has('BEFORE')) {
    for (let index = 0; index < 3; ++index)
        assert(results.get('ZCORE').core[index] <= results.get('BEFORE').core[index],
            `Baseline regressed ${['linked size', 'polling', 'transfer'][index]}`);
}
console.log('All C/assembly protocol, timer validation/recovery, timeout, stack, IX/IY and interrupt-state checks passed.');
