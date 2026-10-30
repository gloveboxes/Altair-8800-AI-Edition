#!/usr/bin/env python3
"""Exercise build discovery and real typed-library builds in an isolated tree."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DCC = Path(os.environ.get("DCC_DIR", str(ROOT.parents[1] / "dcc")))
LINKER = Path(os.environ.get(
    "DCC_L80CC", str(ROOT.parents[1] / "Z80ROMlessSBC/l80cc")))


class BuildAllTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="build all test ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.apps = self.root / "Apps"
        self.scripts = self.root / "scripts"
        self.scripts.mkdir()
        for bucket in ("all", "a", "b", "c", "d"):
            (self.apps / f"disk-{bucket}-apps").mkdir(parents=True)
        shutil.copy2(ROOT / "Apps/build-all.ps1", self.apps)
        shutil.copy2(ROOT / "scripts/build-dcc-app.ps1", self.scripts)
        (self.scripts / "cpm-install.ps1").write_text(
            "param($Image, $HostFile, $CpmFile)\n"
            "$ErrorActionPreference = 'Stop'\n"
            "$installed = Join-Path (Split-Path $Image -Parent) 'installed'\n"
            "New-Item -ItemType Directory -Path $installed -Force | Out-Null\n"
            "Copy-Item -LiteralPath $HostFile -Destination $installed -Force\n"
            "Add-Content -LiteralPath $env:BUILD_TEST_LOG "
            "-Value \"$(Split-Path $Image -Leaf)|$CpmFile\"\n")
        self.log = self.root / "installs.log"
        self.env = {**os.environ, "BUILD_TEST_LOG": str(self.log)}
        self.images = []
        for drive in "ABCD":
            image = self.root / f"{drive}.dsk"
            image.write_bytes(b"untouched disk fixture")
            self.images.append(image)
        self.example = self.apps / "disk-b-apps/LIBDEMO"
        shutil.copytree(
            ROOT / "Apps/disk-b-apps/LIBDEMO", self.example,
            ignore=shutil.ignore_patterns(
                "build", "generated", "*.COM", "*.LIB", "*.SYM",
                "*.MAC", "*.REL", "*.PRN", "*.LNK"))
        self.sdk = self.apps / "disk-b-apps/BANKSDK"
        shutil.copytree(ROOT / "Apps/disk-b-apps/BANKSDK", self.sdk)
        support = self.apps / "disk-b-apps/SUPPORT"
        support.mkdir()
        (support / "MAIN.C").write_text("int main(void) { return 0; }\n")
        (support / "dccmake.txt").write_text(
            "dcc-input=MAIN.C\n"
            "dcc-output=SUPPORT\n"
            "dcc-runtime=${DCC_DIR}/DCCRTL.MAC\n"
            "dcc-include-directory=${DCC_DIR}\n"
            "dcc-tool=${DCC_DIR}/dcc\n"
            "dccpeep-tool=${DCC_DIR}/dccpeep\n"
            "dccrtlstrip-tool=${DCC_DIR}/dccrtlstrip\n"
            "m80c-tool=${DCC_DIR}/m80c\n"
            "l80c-tool=${DCC_DIR}/l80c\n")

    def list_jobs(self, *arguments):
        script = str(self.apps / "build-all.ps1").replace("'", "''")
        result = subprocess.run(
            ["pwsh", "-NoLogo", "-NoProfile", "-Command",
             f"$jobs = @(& '{script}' -ListOnly {' '.join(arguments)}); "
             "ConvertTo-Json -InputObject $jobs -Depth 4"],
            capture_output=True, text=True, env=self.env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def build(self):
        if not (DCC / "dccmake").is_file() or not LINKER.is_file():
            self.skipTest("built typed DCC tools and DCC_L80CC are required")
        arguments = [
            "pwsh", "-NoLogo", "-NoProfile", "-File",
            str(self.apps / "build-all.ps1"),
            "-DccDirectory", str(DCC), "-LibraryLinker", str(LINKER)]
        for drive, image in zip("ABCD", self.images):
            arguments += [f"-Disk{drive}", str(image)]
        return subprocess.run(arguments, capture_output=True, text=True,
                              env=self.env)

    def assert_images_untouched(self):
        for image in self.images:
            self.assertEqual(image.read_bytes(), b"untouched disk fixture")

    def test_discovers_library_before_host_and_ignores_sdk_and_generated_sources(self):
        for relative in ("host/generated", "lib/build-docgen", "tests/nested"):
            directory = self.example / relative
            directory.mkdir(parents=True)
            (directory / "IGNORE.C").write_text("not a standalone app\n")
        jobs = self.list_jobs()
        self.assertEqual(
            [(job["Kind"], job["Output"], job["Drives"]) for job in jobs],
            [("composite", "DATA.LIB,DATATEST.COM", "B"),
             ("dcc", "SUPPORT.COM", "B")])
        self.assertEqual(self.list_jobs("-AssemblyOnly"), [])
        self.assertFalse(self.log.exists())
        self.assert_images_untouched()

    def test_sdk_descendants_are_not_discovered_as_applications(self):
        nested = self.sdk / "nested"
        nested.mkdir()
        (nested / "IGNORE.C").write_text("not a standalone app\n")
        (nested / "IGNORE.ASM").write_text("not a standalone app\n")
        (nested / "dccmake.txt").write_text(
            "dcc-input=IGNORE.C\ndcc-output=IGNORE\n")
        self.assertEqual(len(self.list_jobs()), 2)
        self.assertEqual(self.list_jobs("-AssemblyOnly"), [])
        self.assertFalse(self.log.exists())
        self.assert_images_untouched()

    def test_real_library_generates_contract_before_host_build(self):
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("BUILD ALL PASS: 2 build units", result.stdout)
        self.assertIn("built composite", result.stdout)
        self.assertEqual(self.log.read_text().splitlines(), [
            "B.dsk|0:DATA.LIB", "B.dsk|0:DATATEST.COM", "B.dsk|0:SUPPORT.COM"])
        installed = self.root / "installed"
        library = (installed / "DATA.LIB").read_bytes()
        self.assertEqual(library[:5], b"B1LB\x04")
        self.assertEqual(int.from_bytes(library[12:14], "little"), 8)
        self.assertGreater((installed / "DATATEST.COM").stat().st_size, 1024)
        self.assertTrue((self.example / "host/generated/B1HOST.C").is_file())
        contract = (self.example / "host/generated/API.H").read_text()
        for declaration in ("add16(unsigned int left", "upper(unsigned char * data",
                            "filerd(unsigned char * data",
                            "blkread(unsigned int handle"):
            self.assertIn(declaration, contract)
        self.assertFalse((self.example / "API.H").exists())
        self.assertTrue((self.example / "lib/build/B1ADAPT.C").is_file())
        self.assertFalse((self.example / "host/build").exists())
        self.assert_images_untouched()

    def test_library_failure_stops_before_installing_or_building_host(self):
        source = self.example / "lib/DATA.C"
        source.write_text("#error expected build failure\n" + source.read_text())
        stale = self.example / "lib/DATA.LIB"
        stale.write_bytes(b"stale library")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("BUILD ALL PASS", result.stdout)
        self.assertFalse(self.log.exists())
        self.assertFalse(stale.exists())
        self.assertFalse((self.example / "host/DATATEST.COM").exists())
        self.assert_images_untouched()

    def test_host_failure_does_not_install_successful_library(self):
        source = self.example / "host/DATATEST.C"
        source.write_text("#error expected host failure\n" + source.read_text())
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("project 2/2: host", result.stdout)
        self.assertTrue((self.example / "lib/build/DATA.LIB").is_file())
        self.assertFalse(self.log.exists())
        self.assertNotIn("BUILD ALL PASS", result.stdout)
        self.assert_images_untouched()

    def test_duplicate_composite_output_is_rejected_before_building(self):
        config = self.apps / "disk-b-apps/SUPPORT/dccmake.txt"
        config.write_text(config.read_text().replace(
            "dcc-output=SUPPORT", "dcc-output=DATATEST"))
        result = subprocess.run(
            ["pwsh", "-NoLogo", "-NoProfile", "-File",
             str(self.apps / "build-all.ps1"), "-ListOnly"],
            capture_output=True, text=True, env=self.env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Duplicate application destination: B:DATATEST.COM",
                      result.stdout + result.stderr)
        self.assertFalse(self.log.exists())
        self.assert_images_untouched()

    def test_standalone_library_and_host_configurations_still_build(self):
        (self.example / "dccmake.txt").unlink()
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("BUILD ALL PASS: 3 build units", result.stdout)
        self.assertEqual(self.log.read_text().splitlines(), [
            "B.dsk|0:DATA.LIB", "B.dsk|0:DATATEST.COM", "B.dsk|0:SUPPORT.COM"])
        self.assert_images_untouched()

    def test_rejects_build_directory_that_would_remove_sources(self):
        config = self.example / "lib/dccmake.txt"
        config.write_text(config.read_text().replace(
            "dcc-build-dir=build", "dcc-build-dir=.."))
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Build directory must not be", result.stdout + result.stderr)
        self.assertTrue((self.example / "lib/DATA.C").is_file())
        self.assertFalse(self.log.exists())
        self.assert_images_untouched()


if __name__ == "__main__":
    unittest.main()
