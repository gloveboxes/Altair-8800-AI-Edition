#!/usr/bin/env python3
"""Use the shared Apple container with this project's CP/M disks."""

import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
DISKS = ROOT / "disks" / "512kb"
CONTAINER = Path(os.environ.get(
    "Z80_CPM_CONTAINER", ROOT.parent.parent / "z80-cpm-container")).expanduser().resolve()


def execute(script, arguments):
    target = CONTAINER / "docker" / script
    if not target.is_file():
        raise SystemExit(
            f"Shared container checkout not found: {target}. "
            "Set Z80_CPM_CONTAINER to the z80-cpm-container checkout.")
    os.execv(sys.executable, [sys.executable, str(target), *map(str, arguments)])


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("start", "prepare"):
        raise SystemExit("usage: z80_container.py {start|prepare} [start options]")
    action, arguments = sys.argv[1], sys.argv[2:]
    if action == "prepare":
        if arguments:
            raise SystemExit("prepare does not accept arguments")
        execute("prepare.py", [])
    if any(arg == "--disk-dir" or arg.startswith("--disk-dir=") for arg in arguments):
        raise SystemExit("the ESP32 project always uses disks/512kb")
    execute("start.py", ["--disk-dir", DISKS, *arguments])


if __name__ == "__main__":
    main()
