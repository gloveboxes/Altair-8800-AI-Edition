#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
assert.equal(process.argv.length, 5,
    'Usage: node asm_build_regression.mjs BUILD_DIR DCC_DIR NTVCM');
const [work, dcc, ntvcm] = process.argv.slice(2).map(value => path.resolve(value));
assert(work !== repo && !work.startsWith(repo + path.sep), 'BUILD_DIR must be outside the checkout');
fs.mkdirSync(work, { recursive: true });
const log = path.join(work, 'assembly-build.log');
fs.writeFileSync(log, '');
function run(command, args, succeeds = true) {
    const result = spawnSync(command, args, {
        cwd: repo, encoding: 'utf8', timeout: 180000,
    });
    const output = (result.stdout ?? '') + (result.stderr ?? '');
    fs.appendFileSync(log, `$ ${command} ${args.join(' ')}\n${output}\n`);
    assert(!result.error, String(result.error));
    if (succeeds) assert.equal(result.status, 0, output);
    else assert.notEqual(result.status, 0, 'Invalid source unexpectedly succeeded');
    return output;
}
const images = ['c.dsk', 'b.dsk'].map(name => path.join(work, name));
for (const [index, name] of ['escape-posix.dsk', 'bdsc-v1.60.dsk'].entries())
    fs.copyFileSync(path.join(repo, 'disks', name), images[index]);
const bootTracks = images.map(image => fs.readFileSync(image).subarray(0, 2 * 32 * 137));
const builder = path.join(repo, 'scripts/build-asm-app.ps1');
function build(source, succeeds = true) {
    return run('pwsh', ['-NoProfile', '-File', builder, '-Source', source,
        '-DiskImage', images[0], '-ToolsDiskImage', images[1],
        '-DccDirectory', dcc, '-Ntvcm', ntvcm], succeeds);
}
function extract(image, name, tag) {
    const bytes = fs.readFileSync(image);
    const packed = Buffer.alloc(77 * 32 * 128);
    for (let track = 0; track < 77; ++track) {
        for (let sector = 0; sector < 32; ++sector) {
            const physical = track < 6 ? sector : sector * 17 % 32;
            const offset = (track * 32 + physical) * 137 + (track < 6 ? 3 : 7);
            bytes.copy(packed, (track * 32 + sector) * 128, offset, offset + 128);
        }
    }
    const packedPath = path.join(work, tag + '-packed.dsk');
    const output = path.join(work, tag + '-' + name);
    fs.writeFileSync(packedPath, packed);
    fs.rmSync(output, { force: true });
    run('cpmcp', ['-f', 'altair88', '-T', 'raw', packedPath, '0:' + name, output]);
    assert(fs.existsSync(output), `Missing installed ${name}`);
    return fs.readFileSync(output);
}
const sources = [
    'ATTN/ATTNINF.ASM', 'AUTORUN/AUTORUN.ASM', 'EDIT/EDIT.ASM', 'ENV/ENV.ASM',
    'ESP32/ESP32.ASM', 'FT/FT.ASM', 'INTTEST/INTTEST.ASM', 'SLEEP/SLEEP.ASM',
    'VIBE/VIBE.ASM',
];
for (const source of sources) {
    const fixture = path.join(work, path.basename(source));
    fs.copyFileSync(path.join(repo, 'Apps', source), fixture);
    build(fixture);
    const name = path.parse(fixture).name + '.COM';
    const compiled = fs.readFileSync(path.join(work, name));
    assert.deepEqual(extract(images[0], name, 'c'), compiled);
    if (name === 'FT.COM') assert.deepEqual(extract(images[1], name, 'b'), compiled);
    console.log(`${name}: real M80/L80 build and disk round-trip PASS`);
}
const fixture = path.join(work, 'ASMTEST.ASM');
fs.writeFileSync(fixture, '        ORG 0100H\n        MVI A,42\n        RET\n        END\n');
build(fixture);
const compiled = fs.readFileSync(path.join(work, 'ASMTEST.COM'));
assert.deepEqual(compiled.subarray(0, 3), Buffer.from([0x3e, 42, 0xc9]),
    'COM entry must be at 0100H, without relocation or a prefixed zero page');
const before = images.map(image => fs.readFileSync(image));
fs.writeFileSync(fixture, '        ORG 0100H\n        INVALID OPCODE\n        END\n');
assert.match(build(fixture, false), /M80 reported assembly errors/);
for (const [index, image] of images.entries())
    assert.deepEqual(fs.readFileSync(image), before[index], 'Failed assembly modified a disk');
assert.deepEqual(fs.readFileSync(path.join(work, 'ASMTEST.COM')), compiled,
    'Failed assembly replaced the previous executable');
for (const [index, image] of images.entries())
    assert.deepEqual(fs.readFileSync(image).subarray(0, bootTracks[index].length),
        bootTracks[index], 'Reserved boot tracks changed');
console.log('Absolute entry, fatal-error handling and unchanged disks on failure PASS');
