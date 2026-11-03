#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../../../..');
assert.equal(process.argv.length, 5,
    'Usage: node regression.mjs BUILD_DIR DCC_DIR NTVCM_DIR');
const [work, dcc, ntvcm] = process.argv.slice(2).map(value => path.resolve(value));
assert(work !== repo && !work.startsWith(repo + path.sep),
    'Use a build directory outside the checkout');
fs.mkdirSync(work, { recursive: true });
const env = { ...process.env, DCC_DIR: dcc, NTVCM_DIR: ntvcm,
    PATH: [dcc, ntvcm, process.env.PATH].join(path.delimiter) };

function run(command, args, cwd = repo) {
    const result = spawnSync(command, args, {
        cwd, env, encoding: 'utf8', timeout: 180000, maxBuffer: 4 * 1024 * 1024,
    });
    const output = (result.stdout ?? '') + (result.stderr ?? '');
    assert(!result.error, String(result.error));
    assert.equal(result.status, 0, output);
    return output;
}

for (const name of ['TEST', 'UITEST']) {
    const executable = path.join(work, name.toLowerCase());
    run(process.env.CC ?? 'cc', ['-x', 'c', '-std=c11', '-O2',
        '-Wall', '-Wextra', '-Werror', '-Wstrict-prototypes',
        '-fsanitize=address,undefined', path.join(here, name + '.C'), '-o', executable]);
    console.log(run(executable, []).trim());
}
const app = path.resolve(here, '..');
const targetTests = path.join(work, 'target-tests');
run(path.join(dcc, 'dccmake'), ['dcc-input=tests/TEST.C', 'dcc-output=TEST',
    'dcc-flongio=true', `dcc-build-dir=${targetTests}`], app);
const testOutput = run(path.join(ntvcm, 'ntvcm'),
    ['-n', '-c', '-p', path.join(targetTests, 'TEST.COM')], work);
assert.match(testOutput, /CHESS tests: 0 failures/);
console.log('Real CP/M rules, perft, minimax oracle and state restoration PASS');

const build = path.join(work, 'build');
run(path.join(dcc, 'dccmake'), [`dcc-build-dir=${build}`], app);
const symbols = fs.readFileSync(path.join(build, 'CHESS.SYM'), 'utf8');
const bssEnd = symbols.match(/([0-9A-F]{4})\s+__BSSE\b/);
assert(bssEnd && parseInt(bssEnd[1], 16) + 4096 < 0xf000,
    'Code, static storage and reserved stack must fit CP/M memory');
const core = path.join(repo, 'altair8800');
const memory = path.join(work, 'memory.o');
run(process.env.CC ?? 'cc', ['-I' + core, '-c', path.join(core, 'memory.c'), '-o', memory]);
const fixture = path.join(work, 'chess-z80');
run(process.env.CXX ?? 'c++', ['-x', 'c++', '-std=c++17', '-O2',
    '-Wall', '-Wextra', '-Werror', '-DALTAIR_X80_HOST_TEST=1',
    '-I' + core, '-I' + path.join(core, 'x80_compat'),
    path.join(here, 'Z80.CPP'), path.join(core, 'cpu_x80_adapter.cpp'),
    path.join(core, 'x80.cxx'), '-x', 'none', memory, '-o', fixture]);
console.log(run(fixture, [path.join(build, 'CHESS')], work).trim());

const keyFile = path.join(work, 'QUIT.KEY');
const saveFile = path.join(work, 'CHESS.SAV');
const executable = path.join(build, 'CHESS.COM');
const play = keys => {
    fs.writeFileSync(keyFile, keys);
    return run(path.join(ntvcm, 'ntvcm'), ['-n', '-c', '-f:' + keyFile, executable], work);
};
fs.rmSync(saveFile, { force: true });
const startup = play('2qy');
assert(startup.includes('\x1b[2J\x1b[H\x1b[?25l'));
assert(startup.includes('Your turn (White)'));
assert(startup.includes('QUIT: Y Save & quit'));
assert(startup.includes('\x1b[0m\x1b[?25hThanks for playing CHESS!\r\n'));
assert(startup.includes('Game saved to CHESS.SAV') && fs.existsSync(saveFile));
const restored = play('qn');
assert(restored.includes('Saved game restored') && restored.includes('LEVEL: 2'));
assert(!restored.includes('Game saved') && !fs.existsSync(saveFile));
console.log('CP/M CHESS.SAV save, restore and discard PASS');
console.log(`CHESS regression PASS: ${fs.statSync(executable).size} byte COM; disk images untouched`);
