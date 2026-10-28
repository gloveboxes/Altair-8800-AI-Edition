# MEMPERF: Z80 base RAM and banked-memory copy performance

Build from this folder with the local DCC toolchain:

```sh
export DCC_DIR=/Users/dave/GitHub/dcc
"$DCC_DIR/dccmake"
```

Copy `build/MEMPERF.COM` to a CP/M disk and run `MEMPERF`.
The distributed `MEMPERF.COM` at the folder root is the same release build.
This requires matching BIOS ABI 2.3, extent-allocator firmware, and timer
port `0x17` already used by BANKED. Stopwatch 0 is reset for
each measurement and remains reserved for the duration of the program.

The program allocates 32 KiB in the shared banked heap and a static 32 KiB
buffer in ordinary Z80 memory. It preserves existing heap allocations and
frees its own allocation before exit. A fragmented heap may prevent the
allocation even when the total free size exceeds 32 KiB.

Two cases are measured, each writing 32,768 bytes and then reading all of
them back from banked RAM. Each case also measures ordinary bank-0 SRAM:

* One 32 KiB BANKMEM API call per direction.
* 128 BANKMEM API calls of 256 bytes per direction.

The output places local RAM and banked RAM results side by side, with
separate write/read times and KiB/s. Local RAM uses the DCC runtime's
`memcpy` between two independent 16 KiB halves of the existing 32 KiB
buffer. Each direction copies 16 KiB twice, so the timed payload is still
32 KiB. In bulk mode that is two 16 KiB local copies versus one 32 KiB
banked call. In chunked mode both perform 128 calls of 256 bytes. The
display states this distinction. Two independent 32 KiB base buffers would
not fit in the TPA alongside CP/M and the application.

The banked cases include
BANKMEM allocation validation, BIOS transfer, loop overhead and hardware
I/O delays. Local cases include `memcpy` and loop overhead. Neither includes
allocation, pattern preparation, poisoning,
verification or console output. The comparison shows the cost of repeated
API calls with smaller chunks. It is not a direct measurement of raw SRAM
chip bandwidth.

All source bytes are prepared before the write timer starts. All writes
finish before any reads. The base buffer is filled with inverse expected
values before the read timer starts, and every byte is checked after the
read timer is latched. The address-dependent pattern catches aliased heap
pages; poisoning catches reads which incorrectly report success without
copying data. The local destination is also poisoned before each timed
direction and verified afterwards. Code, stack and CP/M memory are not used
as benchmark destinations.

Timer commands are IN `0x17`, OUT `0x17,0` for parser recovery, OUT
`0x17,0x40` to start stopwatch 0, then OUT `0x17,0x60` followed by four
little-endian IN bytes to latch elapsed milliseconds. Readback/printing of
the latched time is outside the measured interval. Timer resolution is one
millisecond; a zero result is reported as below that resolution, without
dividing by zero. Rates use integer arithmetic with one decimal place.

The source is mirrored in Z80ROMlessSBC/examples/MEMPERF. Emulator checks
live in `src/common/tests/memperf_z80_test.py` in that repository. Scripted
timer values verify the display calculations; hardware timing must be
measured on the board.

All seven MEMPERF emulator checks pass, covering both real bank switching and
a native-copy BIOS, exact bulk/chunked transfer sizes, read/write timing and
rate calculations for local and banked RAM, parser recovery, zero-duration readings, timer failures,
skipped/partial reads, cleanup and an unavailable bank. From the SBC repo:

```sh
build/disk-emulator/bin/python src/common/tests/memperf_z80_test.py
```

The shared BANKMEM library validates allocation capacity through the bank-1
extent SIZE operation. It does not transfer or scan an allocation map in
bank 0. Aligned payloads call READ/WRITE directly; partial transfers retain
the C tail-buffer path. The following timing history predates the extent
allocator and records earlier transfer optimizations.

Measured Z80 cycles for the same MEMPERF timed intervals, converted to
milliseconds at 20 MHz (Pico I/O delays excluded):

| Banked case | Previous write/read | Optimized write/read |
| --- | --- | --- |
| One 32 KiB call | 65.48 / 65.79 ms | 59.96 / 60.27 ms |
| 128 calls of 256 bytes | 939.99 / 940.31 ms | 233.31 / 233.63 ms |

The subsequent assembly scanner and aligned-call changes reduce these same
intervals further:

| Banked case | C scanner | Assembly scanner and aligned path |
| --- | --- | --- |
| One 32 KiB call | 59.96 / 60.27 ms | 59.03 / 59.34 ms |
| 128 calls of 256 bytes | 233.31 / 233.63 ms | 113.79 / 114.20 ms |

A separate BIOS loop change then removes 21 Z80 T-states per 16-byte burst by
using a shorter IX destination-end calculation. The emulator predicts about
2.15 ms saved per 32 KiB direction at 20 MHz, before Pico pauses or wait
states. The BIOS code is in the CP/M boot package. Current firmware mirrors
its FF00h page into bank 1, so both copies match. Upgrade older firmware once
to this mirroring loader; later compatible BIOS updates need only the CP/M
system update. No application COM rebuild is needed for a BIOS optimization.

A further BIOS pass normalizes direction once, keeps the destination in IX,
and restores the caller's stack only on exit. Compared with that 217-byte
BIOS, the current 218-byte implementation measures:

| Banked case | Previous BIOS write/read | Current BIOS write/read |
| --- | --- | --- |
| One 32 KiB call | 56.874 / 57.182 ms | 43.243 / 44.063 ms |
| 128 calls of 256 bytes | 111.203 / 111.513 ms | 95.942 / 96.857 ms |

These are actual emulated Z80 instruction cycles converted to milliseconds
at 20 MHz, excluding Pico pauses. The base-RAM intervals have exactly the
same instruction counts before and after. Deploy the boot package and
matching Drive A system records with page-mirroring firmware. The COM is
unchanged by these BIOS optimizations. Hardware timings remain to be measured.

If initialization fails, the diagnostic COM distinguishes a missing BIOS
descriptor, an unavailable INFO result, a failed header read, and an incorrect
BKM3 heap signature. For example, `00 00 00 00` instead of `42 4b 4d 33`
can result from mismatched resident code in older firmware. Flashing the
page-mirroring firmware fixes that version mismatch without replacing disk
applications. The extra diagnostic code only runs on failure.

Measure the compiled COM's actual instruction cycles from the SBC repository:

```sh
build/disk-emulator/bin/python scripts/benchmark-memperf.py \
  examples/MEMPERF/build/MEMPERF.COM
```

The standalone scanner check executes 97,920 allocation start/length/end-tag
combinations on the Z80 emulator, plus invalid and freed handles:

```sh
build/disk-emulator/bin/python src/common/tests/banked_allocator_z80_test.py
```

These cycle measurements are separate from the scripted stopwatch values
printed by the correctness harness. Run the updated COM on the board to
measure actual wall time. The remaining bulk-copy cost includes loading data
into Z80 registers and switching the whole 64 KiB address space for each
16-byte burst; this scan change does not remove that cost.

The BANKMEM library requires BIOS ABI 2.3 HEAP (`FF10h`). Allocation, free,
size queries, available bytes and reset all run in bank 1. Its reserved
0000h–07FFh region contains extent tables, code and a private stack. The
application heap is 0800h–FEFFh: 63,232 bytes. Install matching firmware,
boot package, drive A system records and rebuilt COM files together.
BANKMEM checks `BKM3BEX1` before executing heap operations.

For allocator timing use [ALLOCPRF](../ALLOCPRF/README.md). MEMPERF times
payload transfers and excludes allocation/free. Verify the extent allocator:

```sh
build/disk-emulator/bin/python src/common/tests/banked_resident_allocator_z80_test.py
```
