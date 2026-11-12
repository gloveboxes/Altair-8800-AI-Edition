#!/usr/bin/env python3
"""Verify ESP32 container entry points always select the project disks."""

import importlib.util
from pathlib import Path
import runpy
import sys
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("z80_container", SCRIPTS / "z80_container.py")
container = importlib.util.module_from_spec(spec)
spec.loader.exec_module(container)


class ContainerEntryTests(unittest.TestCase):
    def test_start_passes_project_disks(self):
        with mock.patch.object(sys, "argv", ["z80_container.py", "start", "--skip-build"]):
            with mock.patch.object(container, "execute") as execute:
                container.main()
        execute.assert_called_once_with("start.py", ["--disk-dir", container.DISKS, "--skip-build"])

    def test_start_rejects_disk_override(self):
        for option in ("--disk-dir", "--disk-dir=/other"):
            with self.subTest(option=option), mock.patch.object(
                    sys, "argv", ["z80_container.py", "start", option]):
                with self.assertRaises(SystemExit):
                    container.main()

    def test_debug_passes_project_disks(self):
        with mock.patch.dict(sys.modules, {"z80_container": container}), mock.patch.object(
                sys, "argv", ["z80-debug-proxy.py", "--workspace", "/work"]):
            with mock.patch.object(container, "execute") as execute:
                runpy.run_path(str(SCRIPTS / "z80-debug-proxy.py"))
        execute.assert_called_once_with(
            "debug_proxy.py", ["--disk-dir", container.DISKS, "--workspace", "/work"])

    def test_debug_rejects_disk_override(self):
        with mock.patch.dict(sys.modules, {"z80_container": container}), mock.patch.object(
                sys, "argv", ["z80-debug-proxy.py", "--disk-dir=/other"]):
            with self.assertRaises(SystemExit):
                runpy.run_path(str(SCRIPTS / "z80-debug-proxy.py"))


if __name__ == "__main__":
    unittest.main()
