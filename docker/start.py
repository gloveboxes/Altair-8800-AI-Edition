#!/usr/bin/env python3
"""Start the shared container with the project disks."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from z80_container import main

sys.argv.insert(1, "start")
main()
