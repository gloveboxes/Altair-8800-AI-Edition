#!/usr/bin/env node
// Requires Node, cc/c++, cpmcp, ntvcm, MAC/LOAD, M80/L80 and the built MCP server.
// All generated files and mutable MCP disk copies stay in the explicit build dir.
// Does not install applications, execute JSON.BAS, or touch deployment images.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn, spawnSync } from 'node:child_process';
import readline from 'node:readline';
import assert from 'node:assert/strict';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../../..');
const args = process.argv.slice(2);
if (args.length !== 4 || args.includes('--help')) {
    console.log('Usage: asm_basic_regression.mjs BUILD_DIR MAC_LOAD_DIR NTVCM M80_L80_DIR');
    console.log('BUILD_DIR must be outside the repository (for example SESSION/files/asm-build).');
    process.exit(args.includes('--help') ? 0 : 2);
}
const [work, tools, ntvcm, microsoft] = args.map(p => path.resolve(p));
assert(!work.startsWith(repo + path.sep) && work !== repo, 'Use a session build directory, not a checkout directory');
fs.mkdirSync(work, { recursive: true });
const logFile = path.join(work, 'asm-basic-regression.log');
fs.writeFileSync(logFile, '');
function log(text) {
    console.log(text);
    fs.appendFileSync(logFile, text + '\n');
}
function run(command, argv, cwd = work) {
    const result = spawnSync(command, argv, {
        cwd, encoding: 'utf8', timeout: 180000, maxBuffer: 8 * 1024 * 1024,
    });
    const output = (result.stdout ?? '') + (result.stderr ?? '');
    fs.appendFileSync(logFile, `$ ${command} ${argv.join(' ')}\n${output}\n`);
    assert(!result.error, String(result.error));
    assert.equal(result.status, 0, output);
    return output;
}
function cpmFile(name, source) {
    const text = source.replace(/\x1a+$/, '').replace(/\r?\n/g, '\r\n');
    fs.writeFileSync(path.join(work, name), text + '\x1a');
}
function readApp(name) {
    return fs.readFileSync(path.join(repo, 'Apps', name), 'utf8');
}
function copyTool(root, name) {
    const actual = fs.readdirSync(root).find(n => n.toUpperCase() === name.toUpperCase());
    assert(actual, `Missing ${name} in ${root}`);
    fs.copyFileSync(path.join(root, actual), path.join(work, name));
}

// Explicitly retained hand-written variants only: do not resurrect retired apps.
const assembly = [
    ['DATE', 'DATE/DATE.ASM'], ['SLEEP', 'SLEEP/SLEEP.ASM'],
    ['INTTEST', 'INTTEST/INTTEST.ASM'], ['ATTNINF', 'ATTN/ATTNINF.ASM'],
    ['ENV', 'ENV/ENV.ASM'], ['ESP32', 'ESP32/ESP32.ASM'],
];
copyTool(tools, 'MAC.COM');
copyTool(tools, 'LOAD.COM');
copyTool(microsoft, 'M80.COM');
copyTool(microsoft, 'L80.COM');
for (const [name, source] of assembly) {
    cpmFile(name + '.ASM', readApp(source));
    const output = run(ntvcm, ['MAC', name]);
    assert(output.includes('END OF ASSEMBLY'), output);
    const listing = fs.readFileSync(path.join(work, name + '.PRN'), 'utf8');
    assert(!/^[A-Z?][ \t]/m.test(listing), `Assembler diagnostics: ${name}`);
    const hex = fs.readFileSync(path.join(work, name + '.HEX'), 'utf8');
    const records = [...hex.matchAll(/:([0-9A-F]{2})([0-9A-F]{4})00([0-9A-F]*)/g)];
    assert(records.length && records[0][2] === '0100', `Empty or relocated HEX: ${name}`);
    assert(records.reduce((n, r) => n + parseInt(r[1], 16), 0) > 200);
    const loaded = run(ntvcm, ['LOAD', name]);
    assert(loaded.includes('FIRST ADDRESS 0100'), loaded);
    assert(fs.statSync(path.join(work, name + '.COM')).size >= 256);
    log(`${name}: real MAC/LOAD PASS`);
}
cpmFile('ATTNZ80.MAC', readApp('ATTN/ATTNZ80.MAC'));
const assembled = run(ntvcm, ['M80', 'ATTNZ80,ATTNZ80=ATTNZ80']);
assert(assembled.includes('No Fatal error(s)'), assembled);
run(ntvcm, ['L80', 'ATTNZ80,ATTNZ80/N/E']);
assert(fs.statSync(path.join(work, 'ATTNZ80.COM')).size > 1000);
log('ATTNZ80: real M80/L80 PASS');

const core = path.join(repo, 'altair8800');
run(process.env.CC ?? 'cc', [
    '-I' + core, '-c', path.join(core, 'memory.c'), '-o', path.join(work, 'memory.o'),
]);
run(process.env.CXX ?? 'c++', [
    '-std=c++17', '-DALTAIR_X80_HOST_TEST=1', '-I' + core,
    '-I' + path.join(core, 'x80_compat'), path.join(here, 'asm_transport_fixture.cpp'),
    path.join(core, 'cpu_x80_adapter.cpp'), path.join(core, 'x80.cxx'),
    path.join(work, 'memory.o'), '-o', path.join(work, 'asm-transport-fixture'),
]);
log(run(path.join(work, 'asm-transport-fixture'), []).trim());

// Extract MBASIC from a packed, read-only copy; never modify the source image.
const pristine = path.join(repo, 'altair_mcp_server/pristine');
const image = fs.readFileSync(path.join(pristine, 'cpm63k.dsk'));
assert(image.length >= 77 * 32 * 137, 'Expected a 137-byte-sector Altair image');
const packed = Buffer.alloc(77 * 32 * 128);
for (let track = 0; track < 77; ++track) {
    for (let sector = 0; sector < 32; ++sector) {
        const physical = track < 6 ? sector : sector * 17 % 32;
        const offset = (track * 32 + physical) * 137 + (track < 6 ? 3 : 7);
        image.copy(packed, (track * 32 + sector) * 128, offset, offset + 128);
    }
}
const packedPath = path.join(work, 'mbasic-readonly-packed.dsk');
fs.writeFileSync(packedPath, packed);
run('cpmcp', ['-f', 'altair88', packedPath, '0:mbasic.com', path.join(work, 'MBASIC.COM')], repo);

const basics = ['COUNT', 'TIME', 'WSPERF', 'FONT'];
const helpers = ['TIME', 'WSPERF', 'FONT'].map(name => readApp(name + '.BAS').split(/\r?\n/)
    .filter(line => /^(?:90|91|92|98)\d\d /.test(line)).join('\n'));
assert(helpers.every(h => h === helpers[0]), 'BASIC helpers diverged; update fixtures deliberately');
for (const name of basics) {
    let source = readApp(name + '.BAS');
    assert(!/\bWAIT\s+\d/i.test(source), `Unbounded legacy WAIT: ${name}`);
    // Bound the test copies even if ntvcm gains a working service backend.
    source = source
        .replace(/\bFOR ([IJ])\s*=\s*0 TO 999999\b/g, 'FOR $1=0 TO 0')
        .replace(/\bWHILE 1\b/g, 'FOR LP=1 TO 1').replace(/\bWEND\b/g, 'NEXT LP')
        .replace(/\bN\s*=\s*10000\b/g, 'N=2')
        .replace(/\bFOR J\s*=\s*1 TO 10\b/g, 'FOR J=1 TO 1')
        .replace(/\bFOR I\s*=\s*33 TO 122\b/g, 'FOR I=33 TO 33');
    assert(!/\b999999\b|\bWHILE 1\b/.test(source), `Unbounded smoke copy: ${name}`);
    cpmFile(name + '.BAS', source.replace(/\bEND\b/g, 'SYSTEM'));
    const output = run(ntvcm, ['MBASIC', name + '.BAS']);
    assert(output.includes(name === 'COUNT' ? 'Loop:' : 'Service failed; transport reset'), output);
    assert(!/\?(?:Syntax|Type|Illegal|Out of|Return)|error in \d+/i.test(output), output);
    log(`${name}.BAS: MBASIC bounded syntax smoke PASS`);
}
const fixtures = [
    ['BIN', 2, 4, [4, 0, 0, 1, 128, 255], 'PASS BYTES 4 DRAIN 4 ACK 0'],
    ['EMPTY', 2, -1, [0, 0], 'PASS BYTES 0 DRAIN 0 ACK 0'],
    ['ACK', 1, 0, [], 'PASS BYTES 0 DRAIN 0 ACK 1'],
    ['ERROR', 255, -1, [3, 0, 21, 110, 111], 'RESET DRAIN 3'],
    ['TOUT', 4, -1, [], 'RESET DRAIN 0'],
    ['LONG', 2, -1, [0, 1, ...Array(256).fill(65)], 'RESET DRAIN 256'],
];
for (const [name, status, expected, data, marker] of fixtures) {
    const helper = helpers[0]
        .replaceAll('IF INP(2)<>0', 'IF 0<>0')
        .replace('OUT 2,G:OUT 3,LEN(Q$) MOD 256:OUT 3,INT(LEN(Q$)/256)',
            'OL=LEN(Q$) MOD 256:OH=INT(LEN(Q$)/256)')
        .replace('OUT 3,ASC(MID$(Q$,X,1))', 'V=ASC(MID$(Q$,X,1))')
        .replace('S=INP(2)', `S=${status}:IF P<4 THEN S=4`)
        .replace('OUT 2,0', 'AK=AK+1')
        .replace('RL=INP(4):RL=RL+256*INP(4)', 'READ RL,RH:RL=RL+256*RH')
        .replace('V=INP(4)', 'READ V:DC=DC+1')
        .replace('OUT 2,1:PRINT "Service failed; transport reset":END',
            'RE=RE+1:PRINT "RESET DRAIN";DC:SYSTEM');
    let source = `10 G=17:Q$=CHR$(1)+CHR$(0):E=${expected}:GOSUB 9000\n`
        + '20 PRINT "PASS BYTES";LEN(R$);"DRAIN";DC;"ACK";AK\n'
        + '30 IF E=4 THEN IF ASC(MID$(R$,1,1))<>0 OR ASC(MID$(R$,4,1))<>255 THEN PRINT "BAD BINARY"\n'
        + '40 SYSTEM\n' + helper + '\n';
    for (let i = 0; i < data.length; i += 40)
        source += `${9900 + i / 40} DATA ${data.slice(i, i + 40).join(',')}\n`;
    cpmFile('T' + name + '.BAS', source);
    const output = run(ntvcm, ['MBASIC', 'T' + name + '.BAS']);
    assert(output.replace(/\s+/g, ' ').includes(marker), output);
    assert(!output.includes('BAD BINARY'), output);
    log(`MBASIC ${name}: counted frame fixture PASS`);
}

// Real MBASIC INP/OUT, actual asynchronous MCP shared services, finite program.
const liveHelper = helpers[0].replace(
    '9800 OUT 2,1:PRINT "Service failed; transport reset":END',
    '9800 OUT 2,1\n9810 IF ER=1 THEN ER=0:RETURN\n9820 PRINT "LIVE BASIC FAIL":SYSTEM',
);
const live = `10 G=17:Q$=CHR$(0)+CHR$(2):E=0:GOSUB 9000
20 G=17:Q$=CHR$(2)+CHR$(2):E=4:GOSUB 9000
30 IF LEN(R$)<>4 THEN GOTO 9820
40 G=16:Q$=CHR$(0)+CHR$(0)+STRING$(4,0):E=0:GOSUB 9000
50 G=16:Q$=CHR$(1)+CHR$(0):E=1:GOSUB 9000
60 IF ASC(R$)<>0 THEN GOTO 9820
70 G=32:Q$=CHR$(0):E=-1:GOSUB 9000
80 IF LEN(R$)=0 THEN GOTO 9820
90 ER=1:G=17:Q$=CHR$(1)+CHR$(3):E=4:GOSUB 9000
100 IF ER<>0 OR INP(2)<>0 THEN GOTO 9820
110 G=32:Q$=CHR$(0):E=-1:GOSUB 9000
120 IF LEN(R$)=0 THEN GOTO 9820
130 OUT 23,66:OUT 23,98:FOR I=1 TO 4:V=INP(23):NEXT I
140 OUT 23,16:OUT 23,0:OUT 23,0:OUT 23,32
150 IF INP(23)<>0 THEN GOTO 9820
160 OUT 23,16:OUT 23,123
170 IF INP(23)<>255 THEN GOTO 9820
180 OUT 23,0:OUT 23,48:IF INP(23)>15 THEN GOTO 9820
190 PRINT "LIVE BASIC PASS":SYSTEM
${liveHelper}\n`;
cpmFile('LIVE.BAS', live);
const mcpDir = path.join(work, 'mcp-basic');
fs.mkdirSync(mcpDir, { recursive: true });
const sourceDisks = ['cpm63k.dsk', 'bdsc-v1.60.dsk', 'blank.dsk', 'blank.dsk']
    .map(name => path.join(pristine, name));
const workingDisks = ['a.dsk', 'b.dsk', 'c.dsk', 'd.dsk'].map(name => path.join(mcpDir, name));
const stderr = fs.openSync(path.join(mcpDir, 'stderr.log'), 'w');
const child = spawn(path.join(repo, 'altair_mcp_server/build/altair-cpm-mcp'),
    [...workingDisks, ...sourceDisks, work],
    { cwd: mcpDir, stdio: ['pipe', 'pipe', stderr] });
let pending;
let sequence = 0;
const lines = readline.createInterface({ input: child.stdout });
lines.on('line', line => {
    try {
        const response = JSON.parse(line);
        if (pending && response.id === pending.id) {
            const current = pending;
            pending = undefined;
            clearTimeout(current.timer);
            if (response.error) current.reject(Error(JSON.stringify(response.error)));
            else current.resolve(response.result.content.map(item => item.text ?? '').join('\n'));
        }
    } catch (error) {
        if (pending) pending.reject(error);
    }
});
child.on('error', error => { if (pending) pending.reject(error); });
child.on('exit', code => { if (pending) pending.reject(Error(`MCP exited: ${code}`)); });
function call(input) {
    return new Promise((resolve, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => reject(Error('MCP response timeout')), 60000);
        pending = { id, timer, resolve, reject };
        child.stdin.write(JSON.stringify({
            jsonrpc: '2.0', id, method: 'tools/call',
            params: { name: 'run_cpm', arguments: { input, cycles: 20000000 } },
        }) + '\n');
    });
}
try {
    let transcript = '';
    for (const command of ['b:', 'ft -g LIVE.BAS', 'a:mbasic live.bas']) {
        const output = await call(command);
        transcript += output;
        fs.appendFileSync(logFile, `MCP> ${command}\n${output}\n`);
    }
    for (let i = 0; i < 30 && !transcript.includes('LIVE BASIC PASS')
            && !transcript.includes('LIVE BASIC FAIL'); ++i) {
        const output = await call('');
        transcript += output;
        fs.appendFileSync(logFile, output + '\n');
    }
    assert(transcript.includes('LIVE BASIC PASS'), transcript);
    assert(!transcript.includes('LIVE BASIC FAIL'), transcript);
    log('MBASIC: actual MCP stopwatch/timer/clock/error-drain/reset/recovery PASS');
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
log('ALL ASSEMBLY/BASIC REGRESSIONS PASS (deployment images untouched)');
