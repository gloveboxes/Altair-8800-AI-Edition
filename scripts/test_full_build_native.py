#!/usr/bin/env python3
"""Build and install every application on temporary native disks."""

import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from scripts.z80_container import CONTAINER

ROOT = Path(__file__).resolve().parents[1]


def run_terminal(test, disks, commands):
    import pty

    master, slave = pty.openpty()
    process = subprocess.Popen(
        [sys.executable, str(CONTAINER / "docker/start.py"), "--skip-build",
         "--disk-dir", str(disks)],
        stdin=slave, stdout=slave, stderr=slave, cwd=ROOT)
    os.close(slave)
    transcript = bytearray()

    def wait_for(needle):
        start = len(transcript)
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            if needle in transcript[start:]:
                return
            ready, _, _ = select.select([master], [], [], 0.1)
            if ready:
                try:
                    transcript.extend(os.read(master, 8192))
                except OSError:
                    break
            if process.poll() is not None:
                break
        test.fail(f"Did not see {needle!r}: {bytes(transcript)!r}")

    try:
        wait_for(b"A>")
        for command, expected in commands:
            os.write(master, command + b"\r")
            wait_for(expected)
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
            process.wait(timeout=30)
        os.close(master)
    test.assertEqual(process.returncode, 0, bytes(transcript))
    return bytes(transcript)


@unittest.skipUnless(os.environ.get("RUN_FULL_APP_BUILD") == "1",
                     "set RUN_FULL_APP_BUILD=1 to run the full application build")
@unittest.skipUnless(sys.platform == "darwin", "Apple container testing requires macOS")
class FullNativeBuildTests(unittest.TestCase):
    def test_build_all_native_disks(self):
        dcc = Path(os.environ["DCC_DIR"])
        ntvcm = Path(os.environ["NTVCM_DIR"]) / "ntvcm"
        with tempfile.TemporaryDirectory(prefix="all native applications ") as name:
            root = Path(name)
            shutil.copytree(ROOT / "Apps", root / "Apps",
                            ignore=shutil.ignore_patterns("build", "__pycache__", ".DS_Store"))
            shutil.copytree(ROOT / "scripts", root / "scripts")
            shutil.copytree(ROOT / "disks/512kb", root / "disks/512kb")
            source = ROOT / "external/Z80ROMlessSBC/src"
            target = root / "external/Z80ROMlessSBC/src"
            (target / "cpm").mkdir(parents=True)
            (target / "disks").mkdir()
            shutil.copyfile(source / "cpm/build_images.py", target / "cpm/build_images.py")
            shutil.copyfile(source / "disks/geometry.py", target / "disks/geometry.py")
            result = subprocess.run(
                ["pwsh", "-NoLogo", "-NoProfile", "-File", str(root / "Apps/build-all.ps1"),
                 "-DccDirectory", str(dcc), "-LibraryLinker", str(dcc / "l80c"),
                 "-Ntvcm", str(ntvcm), "-Python", sys.executable],
                cwd=root, env=os.environ.copy(), text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("BUILD ALL PASS", result.stdout)
            for image in (root / "disks/512kb").glob("*.img"):
                self.assertEqual(image.stat().st_size, 524288)
            run_terminal(self, root / "disks/512kb", [
                (b"B:", b"B>"),
                (b"DATATEST", b"PASS: all 8 bank-1 library checks."),
            ])


if __name__ == "__main__":
    unittest.main()
