#!/usr/bin/env python3
"""Run the shared MI proxy with a snapshot of the project disks."""

import sys
from z80_container import DISKS, execute

if any(arg == "--disk-dir" or arg.startswith("--disk-dir=") for arg in sys.argv[1:]):
    raise SystemExit("the ESP32 project always snapshots disks/512kb")
execute("debug_proxy.py", ["--disk-dir", DISKS, *sys.argv[1:]])
