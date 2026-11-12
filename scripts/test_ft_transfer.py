#!/usr/bin/env python3
"""Exercise the installed shared-service FT.COM under native or Docker CP/M.

Run build-asm-app.ps1 for FT first. All emulator disk writes use temporary
copies. Docker tests run an isolated, automatically removed container.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import pty
import select
import shutil
import subprocess
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]


def extract(directory: Path, drive: str, name: str) -> bytes:
    image = (directory / f"{drive}.dsk").read_bytes()
    skew = (0, 8, 16, 24, 2, 10, 18, 26, 4, 12, 20, 28, 6, 14, 22, 30,
            1, 9, 17, 25, 3, 11, 19, 27, 5, 13, 21, 29, 7, 15, 23, 31)
    filesystem = bytearray()
    for track in range(2, 77):
        for sector in skew:
            physical = sector if track < 6 else sector * 17 % 32
            offset = (track * 32 + physical) * 137 + (3 if track < 6 else 7)
            filesystem.extend(image[offset:offset + 128])
    stem, suffix = name.upper().split(".")
    filename = f"{stem:<8}{suffix:<3}".encode("ascii")
    entries = [filesystem[i:i + 32] for i in range(0, 64 * 32, 32)
               if filesystem[i] == 0 and
               bytes(b & 0x7f for b in filesystem[i + 1:i + 12]) == filename]
    if not entries:
        raise FileNotFoundError(name)
    result = bytearray()
    # cpm64_bios.asm uses EXM=0: each directory entry holds 16 KiB, not 32 KiB.
    for entry in sorted(entries, key=lambda e: e[12] + (e[14] << 5)):
        length = entry[15] * 128
        data = b"".join(filesystem[block * 2048:(block + 1) * 2048]
                        for block in entry[16:16 + (length + 2047) // 2048])
        assert len(data) >= length, name
        result.extend(data[:length])
    return bytes(result)


def boot(directory: Path, docker_image: str | None) -> str:
    container_name = "altair-ft-test-" + uuid.uuid4().hex
    if docker_image:
        command = ["docker", "run", "--rm", "-it", "--name", container_name,
                   "--network", "none",
                   "--mount", f"type=bind,source={directory},target=/test",
                   "--entrypoint", "altair-local", docker_image]
        runtime = Path("/test")
    else:
        command = [str(ROOT / "altair_local/build/altair-local")]
        runtime = directory
    command += ["--apps-root", str(runtime / "apps"), "--env-file", str(runtime / "env.txt")]
    for drive in "abcd":
        command += [f"--drive-{drive}", str(runtime / f"{drive}.dsk")]
    commands = [b"d:\r", b"b:ft -gb binary.bin\r", b"b:ft -gb empty.bin\r",
                b"b:ft -g text.bas\r", b"b:ft -gb missing.bin\r",
                b"b:ft -gb one.bin\r", b"b:ft -gb exact.bin\r",
                b"b:ft -gb tail.bin\r", b"a:\r"]
    master, slave = pty.openpty()
    process = subprocess.Popen(command, cwd=ROOT, stdin=slave, stdout=slave, stderr=slave)
    os.close(slave)
    output = bytearray()
    pending = list(commands)
    deadline = time.monotonic() + 90
    last_prompt = 0
    try:
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.1)
            if not ready:
                continue
            try:
                data = os.read(master, 65536)
            except OSError:
                break
            output.extend(data)
            prompts = output.count(b"A>") + output.count(b"D>")
            if prompts > last_prompt:
                last_prompt = prompts
                if not pending:
                    return output.decode(errors="replace")
                os.write(master, pending.pop(0))
        raise AssertionError(f"FT commands did not finish:\n{output.decode(errors='replace')}")
    finally:
        if process.poll() is None:
            os.write(master, b"\x1d")
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            if docker_image:
                subprocess.run(["docker", "stop", "--time", "1", container_name],
                               check=True, stdout=subprocess.DEVNULL)
            process.terminate()
            process.wait(timeout=10)
        os.close(master)


def main() -> None:
    raise SystemExit("FT tests for 137-byte disks require an appropriate emulator backend")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docker-image", help="test this locally built runtime image")
    args = parser.parse_args()
    binary = bytes(range(256)) * 273 + b"\x00\xffEND"
    text = b'10 PRINT "shared FT"\n20 END\r\n'
    fixtures = {"BINARY.BIN": binary, "EMPTY.BIN": b"", "TEXT.BAS": text,
                "ONE.BIN": b"\xff", "EXACT.BIN": bytes(range(256)),
                "TAIL.BIN": bytes(range(256)) + b"\x00"}
    expected = dict(fixtures)
    expected["TEXT.BAS"] = text.replace(b"\r", b"").replace(b"\n", b"\r\n") + b"\x1a"
    with tempfile.TemporaryDirectory(prefix="altair-ft-test-") as temporary:
        directory = Path(temporary)
        for drive, name in zip("abcd", ("cpm63k.dsk", "bdsc-v1.60.dsk",
                                        "escape-posix.dsk", "blank.dsk")):
            shutil.copyfile(ROOT / "disks" / name, directory / f"{drive}.dsk")
        # The supplied D: image contains apps. Empty only the temporary directory.
        scratch = bytearray((directory / "d.dsk").read_bytes())
        for sector in (0, 8, 16, 24, 2, 10, 18, 26, 4, 12, 20, 28, 6, 14, 22, 30):
            offset = (2 * 32 + sector) * 137
            scratch[offset + 3:offset + 131] = b"\xe5" * 128
            scratch[offset + 132] = sum(scratch[offset + 3:offset + 131]) & 0xff
        (directory / "d.dsk").write_bytes(scratch)
        program = (ROOT / "Apps/disk-c-apps/FT/FT.COM").read_bytes()
        for drive in "bc":
            installed = extract(directory, drive, "FT.COM")
            assert installed == program + b"\0" * (-len(program) % 128), drive
        shutil.copyfile(ROOT / "altair_mcp_server/pristine/bdsc-v1.60.dsk",
                        directory / "reset.dsk")
        assert extract(directory, "reset", "FT.COM") == program + b"\0" * (-len(program) % 128)
        apps = directory / "apps"
        apps.mkdir()
        for name, payload in fixtures.items():
            (apps / name).write_bytes(payload)
        (directory / "env.txt").write_text("")
        output = boot(directory, args.docker_image)
        assert "Bad Sector" not in output and "Bdos Err" not in output, output
        for name, payload in expected.items():
            received = extract(directory, "d", name)
            assert received == payload + b"\0" * (-len(payload) % 128), (
                f"{name}: received {len(received)}, expected {len(payload)} bytes; "
                f"first difference {next((i for i, (a, b) in enumerate(zip(received, payload)) if a != b), None)}\n"
                + output)
            assert f"Done ({len(fixtures[name])} bytes)" in output, output
            print(f"PASS: {name}: {len(payload)} bytes and CP/M record padding match")
        assert "Failed - file not found or server error" in output, output
        try:
            extract(directory, "d", "MISSING.BIN")
        except FileNotFoundError:
            pass
        else:
            raise AssertionError("Failed transfer left MISSING.BIN on disk")
        print("PASS: missing-file error followed by successful downloads")
    print(f"PASS: shared-service FT under {'Docker' if args.docker_image else 'Altair Local'} CP/M")


if __name__ == "__main__":
    main()
