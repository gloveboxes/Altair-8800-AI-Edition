#!/usr/bin/env python3
"""Run real ISAM clients with the Z80ROMlessSBC two-bank CP/M emulator."""

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import unittest


APP = Path(__file__).resolve().parents[1]
REPO = APP.parents[2]
SBC = Path(os.environ.get(
    "Z80SBC_DIR", str(REPO.parents[1] / "Z80ROMlessSBC")))
DCC = Path(os.environ.get("DCC_DIR", str(REPO.parents[1] / "dcc")))
sys.path.insert(0, str(SBC / "src/common/tests"))
import bank1_system_boot_z80_test as boot
import cpm_boot_z80_test as cpm


def build(directory, *options):
    result = subprocess.run(
        [str(DCC / "dccmake"), *options], cwd=directory,
        env={**os.environ, "DCC_DIR": str(DCC),
             "PATH": str(DCC) + os.pathsep + os.environ["PATH"]},
        capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)


class BankedIsam(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build(APP)
        build(APP / "tests")
        cls.extra_builds = tempfile.TemporaryDirectory(prefix="isam-build-test-")
        cls.addClassCleanup(cls.extra_builds.cleanup)
        cls.extra = Path(cls.extra_builds.name)
        build(cls.extra, f"dcc-input={APP / 'pktest.c'},{APP / 'isamdb.c'}",
              "dcc-output=PKSTAND", "dcc-stack-check=true",
              "dcc-stack-bytes=1536", f"dcc-include-directory={DCC},{APP}",
              f"dcc-runtime={DCC / 'DCCRTL.MAC'}")
        sdk = REPO / "Apps/disk-b-apps/BANKSDK"
        inputs = [APP / "tests/PAGETST.C", APP / "isclient.c",
                  APP / "generated/B1HOST.C",
                  *(sdk / name for name in ("BANKLIB.C", "BANKMEM.C", "BIOSBANK.C"))]
        build(cls.extra, "dcc-input=" + ",".join(map(str, inputs)),
              "dcc-output=PAGETST", "dcc-stack-check=true",
              "dcc-stack-bytes=2048", f"dcc-build-dir={cls.extra / 'pages'}",
              f"dcc-include-directory={DCC},{APP},{APP / 'generated'},{sdk}",
              f"dcc-runtime={DCC / 'DCCRTL.MAC'}")
        for source, output, directory, stack in (
                ("FIXGEN.C", "DOCGEN", "fixture", 768),
                ("GENTST.C", "GENTST", "generator", 768),
                ("IPGTST.C", "IPGTST", "indexed", 1536),
                ("FIXDOC.C", "DOCTOR", "fixdoc", 2048)):
            build(cls.extra, "dcc-input=" + ",".join(map(str, [
                APP / "tests" / source, *inputs[1:]])),
                f"dcc-output={output}", "dcc-stack-check=true",
                f"dcc-stack-bytes={stack}", f"dcc-build-dir={cls.extra / directory}",
                f"dcc-include-directory={DCC},{APP},{APP / 'generated'},{sdk}",
                f"dcc-runtime={DCC / 'DCCRTL.MAC'}")
        boot.BankOneSystemBoot.setUpClass()
        fixture = cls()
        fixture.setUp()
        try:
            fixture.load_library()
            output = fixture.command("DOCGEN")
            if b"Generated 8 doctors, 240 patients and 720 history records." not in output:
                raise AssertionError(output.decode(errors="replace"))
            cls.fixture_disk = bytes(fixture.case.disks[3])
        finally:
            fixture.doCleanups()

    def setUp(self):
        self.system = boot.BankOneSystemBoot()
        self.system.setUp()
        self.case = self.system.case
        if hasattr(self, "fixture_disk"):
            self.case.disks[3][:] = self.fixture_disk
        self.temporary = tempfile.TemporaryDirectory(prefix="isam-bank-test-")
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.definitions = self.folder / "diskdefs"
        self.definitions.write_text(cpm.build_images.cpm_diskdefs())
        self.disk = self.folder / "drive.img"
        self.env = {**os.environ, "DISKDEFS": str(self.definitions)}
        self.disk.write_bytes(self.case.disks[3])
        self.library_calls = 0
        self.bank_selections = 0
        self.tstates = 0
        self.slice_budget = 0
        self.phase_starts = {}
        self.measurements = {}
        self.terminal_output = {}
        original_output = self.case.machine

        def output(port, value):
            if port & 255 == 7:
                self.bank_selections += 1
                self.system.banks[self.system.active][:] = original_output.memory
                self.system.active = value & 1
                original_output.memory[:] = self.system.banks[self.system.active]
                if self.system.active == 1:
                    self.library_calls += 1
            else:
                self.case.write_port(port, value)
                if port & 255 == 5 and (
                        1 <= (value & 127) <= 13 or
                        (value & 127) in (20, 21, 30, 31, 32, 34, 35, 40, 41, 42, 43, 44, 50, 51, 52, 55, 56, 57, 58, 59, 60, 61, 62, 63, 66, 67, 68, 69, 70)):
                    # z80 1.0.0's public u32 getter decodes its high byte wrongly.
                    remaining = int.from_bytes(
                        original_output._StateBase__ticks_to_stop, "little")
                    snapshot = (
                        self.tstates + self.slice_budget - remaining,
                        self.bank_selections, self.library_calls,
                        self.case.reads, len(self.case.writes), len(self.case.output))
                    if value < 128:
                        self.phase_starts[value] = snapshot
                    else:
                        before = self.phase_starts[value - 128]
                        self.measurements[value - 128] = dict(zip(
                            ("tstates", "selections", "entries", "reads", "writes",
                             "console_bytes"),
                            (after - start for after, start in zip(snapshot, before))))
                        if value - 128 in (61, 62, 63, 66, 67, 68, 69, 70):
                            self.terminal_output[value - 128] = bytes(
                                self.case.output[before[-1]:])

        original_output.set_output_callback(output)
        for name in ("DOCTOR.COM", "DOCGEN.COM", "ISAM.LIB", "BANKTST.COM"):
            self.remove(name)
        self.install(self.extra / "fixdoc/DOCTOR.COM")
        self.install(self.extra / "fixture/DOCGEN.COM")
        self.install(APP / "tests/build/BANKTST.COM")
        self.case.disks[3][:] = self.disk.read_bytes()

    def boot_drive(self):
        self.case.until_prompt()
        self.case.command("D:")

    def remove(self, name):
        result = subprocess.run(
            ["cpmrm", "-f", "z80sbc", str(self.disk), "0:" + name],
            cwd=self.folder, env=self.env, capture_output=True, text=True)
        if result.returncode and "No files matched" not in result.stderr:
            raise RuntimeError(result.stdout + result.stderr)

    def install(self, source):
        subprocess.run(
            ["cpmcp", "-f", "z80sbc", str(self.disk), str(source),
             "0:" + source.name], cwd=self.folder, env=self.env, check=True,
            capture_output=True, text=True)

    def load_library(self):
        self.disk.write_bytes(self.case.disks[3])
        self.install(APP / "lib/build/ISAM.LIB")
        self.case.disks[3][:] = self.disk.read_bytes()
        self.boot_drive()

    def run_until(self, start, sentinel, timeout=180):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.slice_budget = 1000000
            self.case.machine.ticks_to_stop = self.slice_budget
            self.case.machine.run()
            remaining = int.from_bytes(
                self.case.machine._StateBase__ticks_to_stop, "little")
            self.tstates += (self.slice_budget - remaining) & 0xFFFFFFFF
            self.assertFalse(
                self.case.machine.halted,
                f"bank={self.system.active} PC={self.case.machine.pc:04x} "
                f"output={bytes(self.case.output[-2000:])!r}")
            output = bytes(self.case.output[start:])
            if sentinel in output:
                # The BIOS may still be returning from banked console output.
                if sentinel == b"\r\nD>" and self.system.active != 0:
                    continue
                return output
            if output.endswith(b"\r\nD>"):
                self.fail(
                    f"Client exited before {sentinel!r}: {output[-4000:]!r}; "
                    f"loader header={self.system.banks[1][0xE400:0xE41A].hex()}")
        self.fail(
            f"Emulator timeout: bank={self.system.active} "
            f"PC={self.case.machine.pc:04x} output={output[-4000:]!r}")

    def command(self, text):
        start = len(self.case.output)
        self.case.input.extend(text.encode("ascii") + b"\r")
        output = self.run_until(start, b"\r\nD>")
        self.assertEqual(self.system.active, 0, output.decode(errors="replace"))
        self.assertFalse(self.case.input)
        return output

    def assert_clean_rejections(self):
        for phase in (30, 31, 32, 34, 35):
            self.assertEqual(self.measurements[phase]["writes"], 0,
                             (phase, self.measurements[phase]))

    def test_native_client_size_budget(self):
        sizes = {}
        for name in ("ISCLIENT", "B1HOST", "BANKLIB", "BANKMEM", "BIOSBANK"):
            lines = (APP / f"doctor/build/{name}.LNK").read_text().splitlines()
            sizes[name] = int(next(
                line.split()[-1] for line in lines if line.startswith("size code ")))
        # Previous compatibility facade + generated wrappers: 11,418 bytes.
        # Includes generic slot/key gathering, projections and page loaders.
        self.assertLessEqual(sizes["ISCLIENT"] + sizes["B1HOST"], 6 * 1024, sizes)
        self.assertLessEqual(sum(sizes.values()), 12 * 1024, sizes)
        self.assertLessEqual((APP / "doctor/build/DOCTOR.COM").stat().st_size, 38 * 1024)
        self.assertLessEqual((APP / "docgen/build/DOCGEN.COM").stat().st_size, 18 * 1024)
        library = (APP / "lib/build/ISAM.LIB").read_bytes()
        heap = int.from_bytes(library[24:26], "little")
        stack = int.from_bytes(library[16:18], "little")
        self.assertEqual(stack, 1024)
        self.assertGreaterEqual(0xC800 - stack - heap, 4096 + 4 + 512)

    def test_missing_library_is_reported_by_both_clients(self):
        self.remove("DOCGEN.COM")
        self.install(APP / "docgen/build/DOCGEN.COM")
        self.case.disks[3][:] = self.disk.read_bytes()
        self.boot_drive()
        for program in ("DOCGEN", "DOCTOR"):
            output = self.command(program)
            self.assertIn(b"Cannot load ISAM.LIB: bank error", output)
            self.assertNotIn(b"Generated 8 doctors", output)

    def test_production_generator_fits_disk_and_verifies(self):
        for name in ("BANKTST.COM", "DOCGEN.COM", "DOCTOR.COM"):
            self.remove(name)
        self.install(self.extra / "generator/GENTST.COM")
        self.install(APP / "docgen/build/DOCGEN.COM")
        self.install(APP / "doctor/build/DOCTOR.COM")
        self.case.disks[3][:] = self.disk.read_bytes()
        self.load_library()
        output = self.command("GENTST")
        self.assertNotIn(b"FAIL:", output, output.decode(errors="replace"))
        self.assertIn(b"6 doctors, 300 patients, 1998 history records", output)
        self.assertIn(b"rosters 40-60, histories 5-30", output)
        output = self.command("DOCGEN")
        self.assertIn(b"SURGERY DATABASE GENERATOR - Banked ISAM Library", output)
        self.assertIn(b"Generated 6 doctors, 300 patients and 1998 history records.",
                      output, output.decode(errors="replace"))
        self.assertNotIn(b"failed", output)
        start = len(self.case.output)
        self.case.input.extend(b"DOCTOR /V\r")
        output = self.run_until(start, b"Q Quit")
        self.assertIn(b"DOCTORS SURGERY - Banked ISAM Library", output)
        self.assertIn(b"All relationships and roster links resolved through indexes", output)
        for table, count in ((b"DOCTORS", 6), (b"PATIENTS", 300), (b"HISTORY", 1998)):
            self.assertRegex(output, table + rb"\s+records=" + str(count).encode() +
                             rb"\s+index=" + str(count).encode() +
                             rb"\s+deleted=0\s+errors=0")
        self.assertIn(b"006   Dr", output)
        self.case.input.extend(b"/")
        self.run_until(len(self.case.output), b"Search doctor name:")
        self.case.input.extend(b"Khan\r")
        output = self.run_until(len(self.case.output), b"Q Quit")
        self.assertIn(b"\x1b[7m006   Dr", output)
        self.case.input.extend(b"\r")
        output = self.run_until(len(self.case.output), b"/ Search all patients")
        self.assertIn(b"Farid Khan", output)
        start = len(self.case.output)
        self.case.input.extend(b"\x1b[B" * 19)
        self.run_until(start, b"\x1b[7m00260")
        start = len(self.case.output)
        self.case.input.extend(b"\x1b[B")
        output = self.run_until(start, b"00280")
        self.assertIn(b"\x1b[7m00261", output)
        self.assertNotIn(b"\x1b[2J", output)
        calls = self.library_calls
        start = len(self.case.output)
        self.case.input.extend(b"\x1b[B")
        self.run_until(start, b"\x1b[7m00262")
        self.assertEqual(self.library_calls, calls)
        start = len(self.case.output)
        self.case.input.extend(b"\x1b[A" * 2)
        output = self.run_until(start, b"\x1b[7m00260")
        self.assertIn(b"00241", output)
        start = len(self.case.output)
        self.case.input.extend(b"\x1b[B" * 40)
        self.run_until(start, b"\x1b[7m00300")
        self.case.input.extend(b"\r")
        output = self.run_until(len(self.case.output), b"A Add note")
        self.assertIn(b"Patient 00300", output)
        self.assertIn(b"20230121", output)
        start = len(self.case.output)
        self.case.input.extend(b"\x1b[B" * 3)
        output = self.run_until(start, b"20230621")
        self.assertIn(b"\x1b[7m20230421", output)
        calls = self.library_calls
        start = len(self.case.output)
        self.case.input.extend(b"\x1b[B")
        output = self.run_until(start, b"\x1b[7m20230521")
        self.assertEqual(self.library_calls, calls)
        self.assertNotIn(b"\x1b[K", output)
        start = len(self.case.output)
        self.case.input.extend(b"\x1b[A" * 2)
        self.run_until(start, b"\x1b[7m20230321")
        start = len(self.case.output)
        self.case.input.extend(b"\x1b[B" * 27)
        self.run_until(start, b"\x1b[7m20250621")
        self.case.input.extend(b"q")
        self.run_until(len(self.case.output), b"/ Search all patients")
        self.case.input.extend(b"q")
        self.run_until(len(self.case.output), b"Q Quit")
        self.case.input.extend(b"q")
        self.run_until(len(self.case.output), b"\r\nD>")
        self.assertEqual(self.system.active, 0)
        self.disk.write_bytes(self.case.disks[3])
        listing = subprocess.run(
            ["cpmls", "-f", "z80sbc", "-l", str(self.disk), "0:*"],
            cwd=self.folder, env=self.env, check=True, capture_output=True, text=True)
        geometry = cpm.build_images
        block = geometry.RECORD_BYTES << geometry.DPB_BSH
        allocated = sum(
            ((int(line.split()[1]) + block - 1) // block) * block
            for line in listing.stdout.splitlines() if line.startswith("-"))
        directory_blocks = (
            (geometry.DPB_AL0 << 8) | geometry.DPB_AL1).bit_count()
        capacity = (geometry.DPB_DSM + 1 - directory_blocks) * block
        self.assertEqual(capacity, 502 * 1024)
        self.assertLess(allocated, 448 * 1024, listing.stdout)
        print(f"\nPRODUCTION DISK allocated={allocated} free={capacity - allocated}",
              flush=True)

    def test_indexed_pages_have_constant_depth_cost(self):
        self.remove("BANKTST.COM")
        self.install(self.extra / "indexed/IPGTST.COM")
        self.case.disks[3][:] = self.disk.read_bytes()
        self.load_library()
        output = self.command("IPGTST")
        self.assertNotIn(b"FAIL:", output, output.decode(errors="replace"))
        self.assertIn(b"PASS: indexed pages, constant-depth cost", output)
        first, last = self.measurements[55], self.measurements[56]
        cold = self.measurements[57]
        print(f"\nINDEX PAGE first={first} last={last} uncached={cold}", flush=True)
        self.assertEqual(first["entries"], last["entries"])
        self.assertEqual(first["selections"], last["selections"])
        self.assertEqual(first["writes"], 0)
        self.assertEqual(last["writes"], 0)
        self.assertLess(last["tstates"], first["tstates"] * 1.15)
        self.assertLessEqual(last["reads"], first["reads"] + 2)
        self.assertLessEqual(cold["reads"], 7)
        self.assertEqual(cold["writes"], 0)
        self.assertIn(b"PASS: streamed indexes, interleaved cursors", output)
        self.assertIn(b"PASS: index streaming across 64 KiB", output)
        for phase in (58, 59, 60):
            print(f"INDEX WORK {phase}: {self.measurements[phase]}", flush=True)
            self.assertEqual(self.measurements[phase]["writes"], 0)
            self.assertEqual(self.measurements[phase]["selections"],
                             136 if phase == 60 else 378)
            self.assertEqual(self.measurements[phase]["reads"],
                             0 if phase == 60 else 2)
        # Pre-fast-path T-state baselines; descending batches retain binary search.
        self.assertLess(self.measurements[58]["tstates"], 1878886 * 0.6)
        self.assertLess(self.measurements[59]["tstates"], 1878893 * 1.05)
        self.assertLess(self.measurements[60]["tstates"], 875099 * 0.8)
        self.assertLess(first["tstates"], 261732 * 0.95)
        self.assertLess(last["tstates"], 267474 * 0.95)

    def test_multi_index_regression_and_copied_boundary(self):
        for name in ("DOCTOR.COM", "DOCGEN.COM", "SURGERY.CFG",
                     "DOCTORS.DAT", "DOCTORS.IDX", "PATIENTS.DAT",
                     "PATIENTS.IDX", "PATIENTS.IX1", "PATIENTS.IX2",
                     "PATIENTS.IX3", "HISTORY.DAT", "HISTORY.IDX"):
            self.remove(name)
        self.case.disks[3][:] = self.disk.read_bytes()
        self.load_library()
        output = self.command("BANKTST")
        self.assertNotIn(b"FAIL:", output, output.decode(errors="replace"))
        self.assertIn(b"SUCCESS: multi-key insert", output)
        self.assertIn(b"PASS: copied buffers, 128-byte records", output)
        self.assertIn(b"PASS: bulk tails, projections, partial commits", output)
        self.assertIn(b"PASS: packed fetch order, duplicate slots, guards", output)
        self.assertIn(b"PASS: batched primary/secondary keys, projected ranges", output)
        self.assertIn(b"PASS: banked ISAM regression.", output)
        self.assertIn(b"PASS: composite keys, projected scans, sorted runs", output)
        self.assertIn(b"PASS: sorted build and rebuild cross 64 KiB", output)
        self.assertEqual(self.measurements[52]["reads"], 0)
        self.assertEqual(self.measurements[52]["writes"], 0)
        self.assertIn(b"PASS: 4000-entry index crosses 64 KiB", output)
        self.assert_clean_rejections()
        self.assertGreater(self.library_calls, 100)
        single, bulk = self.measurements[20], self.measurements[21]
        print(f"\nINSERT single={single} bulk={bulk}", flush=True)
        self.assertLess(bulk["selections"], single["selections"] * 0.5)
        self.assertLess(bulk["tstates"], single["tstates"])
        self.assertEqual(bulk["writes"], single["writes"])
        self.assertEqual(single["writes"], 367)
        self.assertLess(single["tstates"], 62067570 * 0.98)
        self.assertLess(bulk["tstates"], 57722127 * 0.98)
        self.assertLessEqual(single["reads"], 4180)
        self.assertLessEqual(bulk["reads"], 4020)
        self.assertLessEqual(single["tstates"], 58613738 * 1.01)
        self.assertLessEqual(bulk["tstates"], 54455535 * 1.01)
        self.assertLess(single["selections"], 2416)
        self.assertLessEqual(bulk["selections"], 722)
        scalar_build, sorted_build = self.measurements[50], self.measurements[51]
        print(f"BUILD scalar={scalar_build} sorted={sorted_build}", flush=True)
        self.assertLess(sorted_build["tstates"], scalar_build["tstates"] * 0.5)
        self.assertLess(sorted_build["reads"], scalar_build["reads"])
        self.assertLess(sorted_build["writes"], scalar_build["writes"] * 0.5)

    def test_visible_pages_are_batched_and_highlight_is_local(self):
        self.install(self.extra / "pages/PAGETST.COM")
        self.case.disks[3][:] = self.disk.read_bytes()
        self.load_library()
        output = self.command("PAGETST")
        self.assertNotIn(b"FAIL:", output, output.decode(errors="replace"))
        self.assertIn(b"PASS: actual UI loaders batch records", output)
        self.assertIn(b"PASS: batched relationship verification detects", output)
        self.assertIn(b"PASS: 2560-byte workspace holds 999 visits", output)
        self.assertIn(b"PASS: 45-doctor directory pages directly", output)
        for phase in (61, 62, 63, 66, 67):
            print(f"VT100 {phase}: {self.measurements[phase]}", flush=True)
        # Both versions flush stdout inside each measured rendering phase.
        limits = (
            (61, 2665752 * 0.9, 1736 * 0.85),
            (62, 176629 * 0.95, 158 * 0.95),
            (63, 450987 * 0.25, 306 * 0.25),
            (66, 1040233 * 0.85, 607 * 0.8),
            (67, 199781 * 0.95, 178 * 0.95),
        )
        for phase, cpu, traffic in limits:
            self.assertLess(self.measurements[phase]["tstates"], cpu)
            self.assertLess(self.measurements[phase]["console_bytes"], traffic)
            self.assertEqual(self.measurements[phase]["writes"], 0)
        for phase in (62, 63, 67):
            self.assertEqual(self.measurements[phase]["entries"], 0)
            self.assertEqual(self.measurements[phase]["reads"], 0)
            self.assertEqual(self.measurements[phase]["writes"], 0)
        self.assertEqual(
            self.terminal_output[63],
            b"\x1b[12;3H20230102   Registration"
            b"\x1b[17;3H\x1b[7m20230202   Consultation\x1b[0m")
        self.assertEqual(self.terminal_output[61].count(b"\x1b[K"), 20)
        self.assertEqual(self.terminal_output[66].count(b"\x1b[K"), 15)
        for phase in (62, 67):
            self.assertEqual(self.terminal_output[phase].count(b"\x1b[K"), 2)
        for row in range(16, 26):
            self.assertIn(f"\x1b[{row};1H\x1b[K".encode(), self.terminal_output[68])
        for row in range(17, 27):
            self.assertIn(f"\x1b[{row};1H\x1b[K".encode(), self.terminal_output[69])
        self.assertEqual(self.terminal_output[70],
                         b"\x1b[12;29H" + b"X" * 48 +
                         b"\x1b[13;29H" + b"X" * 33)
        scalar, batch = self.measurements[40], self.measurements[41]
        full = self.measurements[42]
        old_verify, bulk_verify = self.measurements[43], self.measurements[44]
        print(f"\nPAGE scalar={scalar} full={full} projected={batch}", flush=True)
        print(f"RELATIONS scalar={old_verify} batch={bulk_verify}", flush=True)
        self.assertLess(batch["selections"], scalar["selections"] * 0.5)
        self.assertLess(batch["tstates"], scalar["tstates"] * 0.25)
        self.assertLess(batch["selections"], full["selections"] * 0.6)
        # Packing trades CPU for a smaller transfer on already-cached records.
        self.assertLess(batch["tstates"], full["tstates"] * 1.4)
        self.assertLess(batch["tstates"], 833702 * 0.8)
        self.assertLess(bulk_verify["tstates"], 148189380 * 0.5)
        self.assertLess(bulk_verify["selections"], old_verify["selections"] * 0.5)
        self.assertLess(bulk_verify["tstates"], old_verify["tstates"])
        self.assertEqual(full["writes"], 0)
        self.assertEqual(full["reads"], 0)
        self.assertEqual(old_verify["writes"], 0)
        self.assertEqual(bulk_verify["writes"], 0)
        self.assertEqual(scalar["writes"], 0)
        self.assertEqual(batch["writes"], 0)
        self.assertEqual(scalar["reads"], 0)
        self.assertEqual(batch["reads"], 0)

    def test_missing_data_file_fails_without_recreating_database(self):
        self.remove("DOCTORS.DAT")
        self.case.disks[3][:] = self.disk.read_bytes()
        self.load_library()
        output = self.command("DOCTOR")
        self.assertIn(b"Cannot open ISAM database SURGERY.CFG rc=-1", output)
        self.assertNotIn(b"Riverside Doctors Surgery closed.", output)

    def test_standalone_engine_preserves_mutation_and_index_integrity(self):
        self.remove("PKSTAND.COM")
        self.install(self.extra / "build/PKSTAND.COM")
        self.case.disks[3][:] = self.disk.read_bytes()
        self.boot_drive()
        output = self.command("PKSTAND")
        self.assertNotIn(b"FAIL", output, output.decode(errors="replace"))
        self.assertIn(b"SUCCESS: multi-key insert, update, delete, range, verify, rebuild", output)
        self.assert_clean_rejections()

    def test_generate_verify_and_interactive_clients(self):
        self.load_library()
        output = self.command("DOCGEN")
        self.assertIn(
            b"Generated 8 doctors, 240 patients and 720 history records.", output,
            output.decode(errors="replace"))
        self.assertNotIn(b"failed", output)
        self.assertGreater(self.library_calls, 1000)
        self.disk.write_bytes(self.case.disks[3])
        extracted = self.folder / "ISAM.LIB"
        subprocess.run(
            ["cpmcp", "-f", "z80sbc", str(self.disk), "0:ISAM.LIB", str(extracted)],
            cwd=self.folder, env=self.env, check=True, capture_output=True)
        expected = (APP / "lib/build/ISAM.LIB").read_bytes()
        self.assertEqual(extracted.read_bytes()[:len(expected)], expected)
        calls = self.library_calls
        start = len(self.case.output)
        self.case.input.extend(b"DOCTOR /V\r")
        output = self.run_until(start, b"Q Quit")
        self.assertIn(b"All relationships and roster links resolved through indexes", output)
        for table, count in ((b"DOCTORS", 8), (b"PATIENTS", 240), (b"HISTORY", 720)):
            self.assertRegex(output, table + rb"\s+records=" + str(count).encode() +
                             rb"\s+index=" + str(count).encode() +
                             rb"\s+deleted=0\s+errors=0")
        self.assertGreater(self.library_calls, calls)
        self.assertEqual(self.system.active, 0)
        self.assertIn(b"005   Dr", output)
        self.assertNotIn(b"006   Dr", output)
        calls = self.library_calls
        page_start = len(self.case.output)
        self.case.input.extend(b"\x1b[B" * 4)
        output = self.run_until(page_start, b"Women's Health")
        self.assertIn(b"\x1b[7m005   Dr", output)
        self.assertEqual(self.library_calls, calls)
        page_start = len(self.case.output)
        self.case.input.extend(b"\x1b[B")
        output = self.run_until(page_start, b"\x1b[10;1H\x1b[K")
        self.assertIn(b"\x1b[7m006   Dr", output)
        self.assertNotIn(b"001   Dr", output)
        self.assertIn(b"008   Dr", output)
        self.assertNotIn(b"\x1b[2J", output)
        self.assertNotIn(b"Q Quit", output)
        # Includes the control-frame bank entries and one packed payload read.
        self.assertLessEqual(self.library_calls - calls, 40)
        page_start = len(self.case.output)
        self.case.input.extend(b"\x1b[A")
        output = self.run_until(page_start, b"\x1b[0m\x1b[K")
        self.assertIn(b"\x1b[7m005   Dr", output)
        self.assertIn(b"001   Dr", output)
        self.case.input.extend(b"/")
        self.run_until(len(self.case.output), b"Search doctor name:")
        self.case.input.extend(b"Davies\r")
        output = self.run_until(len(self.case.output), b"Q Quit")
        self.assertIn(b"\x1b[7m008   Dr", output)
        self.assertNotIn(b"001   Dr", output)
        self.case.input.extend(b"d")
        output = self.run_until(start, b"Interactive session counters")
        self.assertIn(b"ISAM ACTIVITY", output)
        # Deepest normal UI path: roster -> search -> history -> search -> roster.
        self.case.input.extend(b"q")
        self.run_until(len(self.case.output), b"Q Quit")
        for _ in range(3):
            self.case.input.extend(b"\r")
            self.run_until(len(self.case.output), b"/ Search all patients")
            self.case.input.extend(b"p")
            self.run_until(len(self.case.output), b"Search patient name:")
            self.case.input.extend(b"A\r")
            self.run_until(len(self.case.output), b"Enter open patient")
            self.case.input.extend(b"\r")
            self.run_until(len(self.case.output), b"A Add note")
            self.case.input.extend(b"q")
            self.run_until(len(self.case.output), b"Enter open patient")
            self.case.input.extend(b"q")
            self.run_until(len(self.case.output), b"/ Search all patients")
            self.case.input.extend(b"q")
            self.run_until(len(self.case.output), b"Q Quit")
        self.case.input.extend(b"q")
        output = self.run_until(start, b"\r\nD>")
        self.assertIn(b"Riverside Doctors Surgery closed.", output)
        self.assertNotIn(b"failed", output)
        self.assertNotIn(b"stack overflow", output)
        self.assertEqual(self.system.active, 0)
        output = self.command("BANKTST /P")
        self.assertNotIn(b"FAIL:", output, output.decode(errors="replace"))
        self.assertIn(b"PASS: measured ISAM performance workloads.", output)
        self.assertIn(b"MIDPOINT comparisons=20", output)
        self.assertEqual(set(self.measurements), set(range(1, 14)))
        for phase, values in sorted(self.measurements.items()):
            print(f"\nPERF {phase}: {values}", flush=True)
            self.assertEqual(values["writes"], 0)
        m = self.measurements
        self.assertLess(m[1]["tstates"], 982465 * 0.9)
        self.assertLess(m[1]["selections"], 720 * 0.7)
        self.assertLess(m[13]["tstates"], 4834625 * 0.75)
        self.assertLess(m[1]["tstates"], 1377582 * 0.75)
        self.assertLess(m[2]["tstates"], 2907582 * 0.75)
        self.assertLess(m[2]["selections"], 2720 * 0.75)
        self.assertEqual(m[3]["reads"], 0)
        self.assertLess(m[7]["reads"], 1940 * 0.1)
        self.assertLess(m[8]["reads"], 757 * 0.6)
        self.assertLess(m[10]["selections"], m[8]["selections"] * 0.5)
        self.assertLess(m[10]["tstates"], 31915845 * 0.6)
        self.assertLess(m[11]["selections"], m[8]["selections"] * 0.15)
        self.assertLess(m[11]["tstates"], 31915845 * 0.25)
        self.assertLess(m[12]["selections"], m[9]["selections"] * 0.2)
        self.assertLess(m[12]["tstates"], 4367116 * 0.3)
        # Native boundary versus the final compatibility implementation.
        for phase, baseline in ((1, 846185), (2, 1591525), (3, 2797925),
                                (7, 13176183), (8, 32802523), (13, 2770665)):
            self.assertLess(m[phase]["tstates"], baseline * 0.95, (phase, m[phase]))
        for phase, baseline in ((10, 15478445), (11, 5773307), (12, 1013485)):
            self.assertLessEqual(m[phase]["tstates"], baseline * 1.01,
                                 (phase, m[phase]))
        self.assertLessEqual(m[2]["selections"], 800)
        self.assertLessEqual(m[3]["selections"], 480)
        self.assertLessEqual(m[13]["selections"], 1120)


if __name__ == "__main__":
    unittest.main(verbosity=2)
