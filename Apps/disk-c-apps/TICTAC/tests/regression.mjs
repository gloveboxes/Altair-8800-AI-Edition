#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import readline from 'node:readline';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../../../..');
assert.equal(process.argv.length, 5,
    'Usage: node regression.mjs BUILD_DIR DCC_DIR NTVCM_DIR');
const [work, dcc, ntvcm] = process.argv.slice(2).map(value => path.resolve(value));
assert(work !== repo && !work.startsWith(repo + path.sep), 'Use a build directory outside the checkout');
fs.mkdirSync(work, { recursive: true });
const log = path.join(work, 'tictac.log');
fs.writeFileSync(log, '');
const env = { ...process.env, DCC_DIR: dcc, NTVCM_DIR: ntvcm,
    PATH: [dcc, ntvcm, process.env.PATH].join(path.delimiter) };
function run(command, args, cwd = repo) {
    const result = spawnSync(command, args, {
        cwd, env, encoding: 'utf8', timeout: 180000, maxBuffer: 4 * 1024 * 1024,
    });
    const output = (result.stdout ?? '') + (result.stderr ?? '');
    fs.appendFileSync(log, `$ ${command} ${args.join(' ')}\n${output}\n`);
    assert(!result.error, String(result.error));
    assert.equal(result.status, 0, output);
    return output;
}
run(process.env.CC ?? 'cc', ['-std=c99', '-Wall', '-Wextra', '-Werror',
    '-Wno-deprecated-non-prototype', '-fsanitize=address,undefined',
    path.join(here, 'game_test.c'), '-o', path.join(work, 'game-test')]);
const hostOutput = run(path.join(work, 'game-test'), []);
assert.match(hostOutput, /TICTAC game logic PASS/);
assert.match(hostOutput, /TICTAC raw console PASS/);
console.log('Game logic, AI, raw console buffering/input and timer calls PASS');
const build = path.join(work, 'build');
run(path.join(dcc, 'dccmake'), [`dcc-build-dir=${build}`], path.resolve(here, '..'));
const core = path.join(repo, 'altair8800');
run(process.env.CC ?? 'cc', ['-I' + core, '-c', path.join(core, 'memory.c'),
    '-o', path.join(work, 'memory.o')]);
run(process.env.CXX ?? 'c++', ['-std=c++17', '-O2', '-DALTAIR_X80_HOST_TEST=1',
    '-I' + core, '-I' + path.join(core, 'x80_compat'),
    path.join(here, 'z80_game_fixture.cpp'), path.join(core, 'cpu_x80_adapter.cpp'),
    path.join(core, 'x80.cxx'), path.join(work, 'memory.o'),
    '-o', path.join(work, 'z80-game-fixture')]);
console.log(run(path.join(work, 'z80-game-fixture'), [path.join(build, 'TICTAC')]).trim());
console.log(run(path.join(work, 'z80-game-fixture'),
    [path.join(build, 'TICTAC'), 'escape']).trim());

const pristine = path.join(repo, 'altair_mcp_server/pristine');
const sources = ['cpm63k.dsk', 'bdsc-v1.60.dsk', 'blank.dsk', 'blank.dsk']
    .map((name, index) => {
        const target = path.join(work, `source-${index}.dsk`);
        fs.copyFileSync(path.join(pristine, name), target);
        return target;
    });
const executable = path.join(build, 'TICTAC.COM');
run('pwsh', ['-NoProfile', '-File', path.join(repo, 'scripts/cpm-install.ps1'),
    '-Image', sources[2], '-HostFile', executable, '-CpmFile', '0:TICTAC.COM']);
const disks = ['a.dsk', 'b.dsk', 'c.dsk', 'd.dsk'].map(name => path.join(work, name));
const stderr = fs.openSync(path.join(work, 'mcp-stderr.log'), 'w');
const child = spawn(path.join(repo, 'altair_mcp_server/build/altair-cpm-mcp'),
    [...disks, ...sources, path.join(repo, 'Apps')],
    { cwd: work, stdio: ['pipe', 'pipe', stderr] });
const lines = readline.createInterface({ input: child.stdout });
let pending, sequence = 0;
lines.on('line', line => {
    let response;
    try {
        response = JSON.parse(line);
    } catch (error) {
        if (pending) pending.reject(error);
        return;
    }
    if (pending && response.id === pending.id) {
        const current = pending;
        pending = undefined;
        clearTimeout(current.timer);
        if (response.error) current.reject(Error(JSON.stringify(response.error)));
        else current.resolve(response.result.content.map(item => item.text ?? '').join('\n'));
    }
});
child.on('error', error => { if (pending) pending.reject(error); });
child.on('exit', code => { if (pending) pending.reject(Error(`MCP exited: ${code}`)); });
function call(input) {
    assert(!pending, 'Only one MCP request at a time');
    return new Promise((resolve, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => reject(Error('MCP response timeout')), 60000);
        pending = { id, timer, resolve, reject };
        child.stdin.write(JSON.stringify({
            jsonrpc: '2.0', id, method: 'tools/call',
            params: { name: 'run_cpm', arguments: {
                input, cycles: 2000000,
            } },
        }) + '\n');
    }).then(output => {
        fs.appendFileSync(log, `MCP> ${JSON.stringify(input)}\n${output}\n`);
        return output;
    });
}
async function until(marker, first = '') {
    let output = first;
    const deadline = Date.now() + 15000;
    while (!output.includes(marker) && Date.now() < deadline)
        output += await call('');
    assert(output.includes(marker), `Missing ${marker}:\n${output}`);
    assert(!/Service (?:error|failed)|transport error/i.test(output), output);
    return output;
}
try {
    await call('c:');
    for (let game = 0; game < 2; ++game) {
        const startup = await until('Your turn', await call('tictac'));
        assert(startup.includes('\x1b[2J\x1b[H\x1b[?25l\x1b[1;1H\x1b[36mTICTAC'),
            'Incorrect VT100 initialization/title position');
        assert(startup.includes('\x1b[8;1H\x1b[36mYou(X): \x1b[33m0'),
            'Incorrect initial score position/value');
        const cursors = [...startup.matchAll(/\x1b\[(\d+);(\d+)H/g)];
        assert(cursors.length > 200, 'Missing board cursor positioning');
        for (const [, row, column] of cursors) {
            assert(+row >= 1 && +row <= 28 && +column >= 1 && +column <= 80,
                `Out-of-screen cursor position: ${row};${column}`);
        }
        const colors = new Set([0, 30, 31, 32, 33, 34, 35, 36, 37,
            41, 43, 44, 90, 97, 100, 107]);
        for (const [, code] of startup.matchAll(/\x1b\[(\d+)m/g))
            assert(colors.has(+code), `Invalid terminal color: ${code}`);
        const first = await call(' ');
        const output = await until('Your turn', first);
        assert(output.includes('CPU thinking'), 'Human placement did not reach CPU turn');
        assert(output.includes('  OO  '), 'CPU did not place its piece');
        await until('Thanks for playing!', await call('q'));
    }
    console.log('Real Z80/CP-M: exact VT100 setup, cursor/colors/scores, human/CPU turns, Q and relaunch PASS');
} finally {
    if (pending) clearTimeout(pending.timer);
    child.stdin.end();
    lines.close();
    if (child.exitCode === null) {
        const exited = new Promise(resolve => child.once('exit', resolve));
        const timer = setTimeout(() => child.kill('SIGTERM'), 2000);
        await exited;
        clearTimeout(timer);
    }
    fs.closeSync(stderr);
}
console.log(`TICTAC regression PASS: ${fs.statSync(executable).size} bytes; deployment disks untouched`);
