from __future__ import annotations

import tempfile
from pathlib import Path
import unittest

import build_cpm64_bios as builder


class BuildCpm64BiosTest(unittest.TestCase):
    def test_make_bios_zero_fills_uninitialized_storage(self) -> None:
        initialized = {
            builder.BIOS_BASE: 0xC3,
            builder.BIOS_BASE + builder.BIOS_BYTES - 1: 0x5A,
        }
        bios = builder.make_bios(initialized)
        self.assertEqual(len(bios), builder.BIOS_BYTES)
        self.assertEqual(bios[0], 0xC3)
        self.assertEqual(bios[-1], 0x5A)
        self.assertEqual(bios[1:-1], bytes(builder.BIOS_BYTES - 2))

    def test_update_disk_changes_only_bios_payloads_and_checksums(self) -> None:
        disk = bytearray(builder.MINIMUM_DISK_BYTES + 96)
        for sector in range(builder.SECTORS_PER_TRACK, 2 * builder.SECTORS_PER_TRACK):
            frame = sector * builder.SECTOR_BYTES
            disk[frame] = 0x81
            disk[frame + 1] = 0
            disk[frame + 2] = 1
            disk[frame + builder.STOP_OFFSET] = 0xFF

        bios = bytes(index & 0xFF for index in range(builder.BIOS_BYTES))
        updated = builder.update_disk(bytes(disk), bios)
        allowed = set()

        for record in range(builder.BIOS_RECORDS):
            sector = builder.BIOS_FIRST_SECTOR + record
            frame = sector * builder.SECTOR_BYTES
            expected = bios[
                record * builder.PAYLOAD_BYTES : (record + 1) * builder.PAYLOAD_BYTES
            ]
            self.assertEqual(
                updated[
                    frame + builder.PAYLOAD_OFFSET :
                    frame + builder.PAYLOAD_OFFSET + builder.PAYLOAD_BYTES
                ],
                expected,
            )
            self.assertEqual(updated[frame + builder.STOP_OFFSET], 0xFF)
            self.assertEqual(
                updated[frame + builder.CHECKSUM_OFFSET], sum(expected) & 0xFF
            )
            allowed.update(
                range(
                    frame + builder.PAYLOAD_OFFSET,
                    frame + builder.PAYLOAD_OFFSET + builder.PAYLOAD_BYTES,
                )
            )
            allowed.add(frame + builder.STOP_OFFSET)
            allowed.add(frame + builder.CHECKSUM_OFFSET)

        for offset, (before, after) in enumerate(zip(disk, updated)):
            if offset not in allowed:
                self.assertEqual(before, after)

    def test_parse_intel_hex_validates_checksum(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bios.hex"
            path.write_text(":03F90000C300F948\n:00000001FF\n", encoding="ascii")
            self.assertEqual(
                builder.parse_intel_hex(path),
                {0xF900: 0xC3, 0xF901: 0x00, 0xF902: 0xF9},
            )
            path.write_text(":03F90000C300F949\n:00000001FF\n", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "checksum"):
                builder.parse_intel_hex(path)

    def test_sync_system_tracks_preserves_filesystem(self) -> None:
        system = bytes([0x11]) * builder.SYSTEM_TRACK_BYTES + bytes([0x22]) * 64
        mirror = bytes([0x33]) * builder.SYSTEM_TRACK_BYTES + bytes([0x44]) * 64
        synced = builder.sync_system_tracks(mirror, system)
        self.assertEqual(synced[: builder.SYSTEM_TRACK_BYTES], bytes([0x11]) * builder.SYSTEM_TRACK_BYTES)
        self.assertEqual(synced[builder.SYSTEM_TRACK_BYTES :], bytes([0x44]) * 64)


if __name__ == "__main__":
    unittest.main()
