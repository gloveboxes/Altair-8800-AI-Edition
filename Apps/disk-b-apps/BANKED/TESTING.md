# Current extent allocator (ABI 2.3)

The current heap is 0800h–FEFFh (63,232 bytes). It uses separate extent-length,
physical-predecessor and doubly linked free-list tables. All heap operations
execute in bank 1 with a private assembly stack. Earlier sections below record
historical two-bit-map implementations and their results.

The current resident Z80 tests cover allocation rounding, exhaustion, invalid
and interior handles, double free, reset, adjacent-extent coalescing, maximum
fragmentation, and 5,000 deterministic randomized operations against an
independent interval model. They check table/list invariants, cached free
bytes, payload/code preservation, caller stack, IX/IY, bank and interrupt
restoration. An occupied-prefix case has the same Z80 instruction-cycle cost
for 256-byte and 32 KiB allocation/free requests; this is emulator evidence,
not a measured physical-board timing result.

ALLOCPRF measures extent allocation/free pairs with empty, occupied-prefix
and fragmented heaps. It contains no C/map allocator reference. Rebuilt firmware, boot/system images and COM
files must be deployed together before collecting new hardware results.

---

# Resident banked memory verification

The default dcc BANKED.COM is built with the unmodified local dcc runtime and
a 512-byte stack reservation. It uses the existing stopwatch on timer I/O
port `0x17` to count 43,200 seconds; CP/M's calendar clock is not required.
The finite emulator build was executed with the independent local Z80
emulator, real relocated CP/M/BIOS and modeled A16 switching. The banked
emulator fixture models stopwatch 0 on port `0x17`, checking the seconds
read, 10-second UI refresh, completed-iteration count and cumulative
read/write transfer-call counters and average operations per second excluding
setup transfers. The firmware timer
test also checks the port's 32-bit elapsed-seconds value at 12 hours.

For a finite emulator check, build the same source for one complete 24-pattern
cycle into a separate directory and run the banked heap fixture. The normal
12-hour build remains in `build/BANKED.COM`:

```sh
cd examples/BANKED
DCC_DIR=/Users/dave/GitHub/dcc /Users/dave/GitHub/dcc/dccmake \
  dcc-define=BANKED_ONE_CYCLE dcc-build-dir=build/emulator
cd ../..
build/disk-emulator/bin/python src/common/tests/banked_heap_z80_test.py \
  --program examples/BANKED/build/emulator/BANKED.COM
```

All nine emulator checks passed. The fixture includes missing-bank and
old-ABI rejection, all 24 patterns, injected skipped and partial reads and
16 KiB address aliasing, timer parser
recovery from incomplete commands and errors, and simulated 12-hour expiry
without falsely counting an incomplete iteration.

The finite emulator build uses the same 24 patterns as the normal build:
zero, ones, alternating, address-derived and inverse values, and walking
one/zero bits. Each pattern writes the entire bank, then independently reads
and compares it using a poisoned base buffer. The
allocator check covers rounding, small and large allocations, end-of-block
transfers, fragmentation, reuse, complete exhaustion and the final heap byte.
The BIOS metadata and resident service pages are reserved and are not part of
the allocatable extended-RAM range.

The private allocation-size scanner is also executed directly as Z80 code
over 97,920 combinations of starting block, allocation length and end tag,
plus freed, invalid and misaligned handles. IX/IY/SP and the input map must
survive each call:

```sh
build/disk-emulator/bin/python src/common/tests/banked_allocator_z80_test.py
```

The aligned transfer fast path is covered by startup checks for base buffers
below 0100h, crossing FF00h, wrapping at FFFFh, and zero-length requests. Both
the real SBC BIOS and native-copy BIOS model apply the public ABI's bounds
checks; invalid requests return failure rather than touching memory.

Passed:

- Compiled C allocation, 24 full-bank write/read/compare patterns over all
  65,024 usable bytes, bounds checks, free/reuse, exhaustion and FEFFh.
- Missing memory and older B0h firmware ABI exit without switching banks.
- Missing BIOS descriptor and old B2h backend exit without calling an unknown service.
- Public BIOS READ/WRITE reject misaligned counts, overflow and resident-page access.
- INFO returns the documented ABI version and heap geometry.
- The same compiled COM also passes a native-copy BIOS backend, with no port-7
  I/O and the SBC's private workspace poisoned and verified untouched.
- No writes to CP/M DMA addresses 0080h–00FFh during the library operations.
- Resident code in both banks remains unchanged after the complete C example.
- Capability reads are cached: at most two for the complete example.
- Assembly read/write runs of 0, 16, 32 and 256 bytes at low, unaligned and
  upper-boundary addresses preserve SP, IX, IY and maskable interrupt state.
- Boot-image construction tests: 16 passed, including generated code matching
  its assembly source and BIOS/resident-page overlap rejection.
- Independent CP/M execution tests: 19 passed, including autorun, disk writes,
  transient execution and warm reload.
- Native z80-debug-host: boot, CLEAR, DIR, LS, multi-extent ATTNC11 and PIP
  copies across disk tracks, followed by execution of the copied program.
- Phase 9 diagnostic, Phase 9 automatic loader and Phase 10 firmware, boot
  packages and disk images build successfully.

The standard 17-entry BIOS jump table and ZAR1 signature are preserved. The
complete bios.bin is now 1,024 bytes and includes the extension page directly;
its bytes are checked against the supervisor backend generated from the same
assembly source.

The original complete example used 14,950 bank switches; the optimized
example uses 10,488, including additional reserved-page rejection checks.
Full 256-byte data reads/writes now use 16 bursts and 32 bank switches, with
one assembly call for all full bursts. The earlier 12-byte implementation
used 44 switches for a read and 46 for a write because of the partial burst.
The old DMA save/install/restore overhead is eliminated.

The current BIOS extension uses **218 bytes** for code and descriptor, plus
10 bytes of private workspace within the reserved 256-byte page. Direction is
normalized once per call. IX carries the destination end across bursts, and
SP is prepared directly for the next source burst. The caller's real stack is
restored once on exit, while interrupts are still disabled. The write path
also uses DEC A to select bank 0 without a separate XOR.

Measured Z80 instruction costs against the previous 217-byte implementation,
including CALL and return, with interrupts initially disabled:

| Transfer | Previous T-states | Current T-states | Current at 20 MHz |
| --- | ---: | ---: | ---: |
| Read 256 bytes | 9,279 | 7,366 | 368.30 us |
| Write 256 bytes | 9,222 | 7,230 | 361.50 us |
| Read 4,096 bytes | 142,239 | 109,606 | 5.480 ms |
| Write 4,096 bytes | 141,462 | 107,550 | 5.378 ms |

Actual MEMPERF timed intervals, measured as Z80 cycles at 20 MHz:

| Case | Previous write/read | Current write/read |
| --- | --- | --- |
| One 32 KiB call | 56.874 / 57.182 ms | 43.243 / 44.063 ms |
| 128 calls of 256 bytes | 111.103 / 111.513 ms | 95.942 / 96.857 ms |

These exclude Pico pauses and memory wait states; the correctness harness's
scripted stopwatch values are separate. A single 16-byte call takes 3 extra
T-states for write or 7 for read (less than 0.4 us at 20 MHz) because of the
setup moved out of the loop. Multiple-burst calls benefit from the reduced
loop work. The public ABI, burst size and CP/M memory boundary are unchanged.

The direct transfer checks now include maximum-length 65,024-byte calls,
unaligned addresses, ranges ending at FF00h, both directions and both initial
interrupt-enable states. The caller and stack are below 0100h so the largest
legal base buffer cannot overwrite them. The payload varies with both byte
and page address, and the test checks stack/index-register restoration and
an untouched 128-byte low-memory guard.

The resident page comes from the validated CP/M boot package. Firmware
mirrors all 256 bytes into bank 1 and verifies them, ensuring its code matches
bank 0. Upgrade older firmware once to this mirroring loader; later BIOS
changes need only a boot-package and matching Drive A system update.
Application COMs do not need rebuilding for a compatible BIOS change.

The loader regression supplies two different resident pages, including changed
last-page bytes, and checks that bank 1 matches each supplied page exactly.
The flash-loader test also checks that the page comes from offset FF00h in the
CRC-validated image. This prevents the former mismatch between a new bank-0
BIOS and an older copy embedded in firmware.

The reusable benchmark stops at the caller's return address before HALT can
consume remaining emulator ticks, checks the copied data and register state,
and compares 16-, 256- and 4,096-byte calls. The latest captured comparisons
are `build/bios-pass-transfer-cycles.json` and
`build/bios-pass-memperf-cycles.json`; the prior binary is
`build/bios-pass-baseline/banked_transfer.bin`:

```sh
build/disk-emulator/bin/python src/common/tests/banked_transfer_benchmark.py \
  --reference build/bios-pass-baseline/banked_transfer.bin
build/disk-emulator/bin/python scripts/benchmark-memperf.py
```

The full host-control suite now passes **78 of 78** tests.

The hostname failure was a stale expectation: the mock board ID
`0123456789ABCDEF` produces `zeta-80-abcdef` under the documented final-six,
lowercase rule, not `zeta-80-789abc`. The expectation is corrected, with
checks for insufficient capacity, the exact-size buffer and cached board-ID
lookup. No production hostname code changed.

The math-soak and scope emulator failures are fixed. The local core invokes
write callbacks only for addresses marked with WRITE_MARK; their shared Python
fixture registered a callback without marking the injection address or scope
mailbox. It now marks FFFFh for corruption injection and 0F04h–0F13h for scope
mailbox observation. The Z80 diagnostic workload itself needed no changes.
Both diagnostics now check corruption at 2000h, 8000h and FFFFh for all four
patterns, asserting the exact failure record and first-fault HALT. The banked
heap fixture also marks its observed DMA addresses so its no-DMA-write check
uses an active callback.

Commands from the Z80ROMlessSBC repository root:

```sh
build/disk-emulator/bin/python src/common/tests/banked_memory_z80_test.py
build/disk-emulator/bin/python src/common/tests/banked_heap_z80_test.py
build/disk-emulator/bin/python src/cpm/test_build_images.py
build/disk-emulator/bin/python src/common/tests/cpm_boot_z80_test.py
build/disk-emulator/bin/python src/cpm/test_z80_debug_host.py \
  --host build/cpm/z80_debug_host/z80-debug-host --assembler z80asm
```

The PIO-specific host tests exercise the real PIO C driver through an
instrumented HAL and execute the assembled 31-word PIO program in a digital
model. They cover allocation fallback, nested interrupt masking, slow-IN data
pin turnaround, bank preservation across bus grants, timeouts, every D0–D7
value at six clock phases, port aliases, slow-service routing, BUSACK gating
and a full progress FIFO. The model checks minimum clock pulse lengths and
the eight-tick WAIT-release guard. The Z80 emulator tests above continue to
exercise the BIOS and allocator; they do not emulate RP2350 peripherals.

The physical board boots and passes this example at 20 MHz using both the
PWM/software and PIO backends. With PIO active, the user confirmed one power
cycle and five or six supervisor reboots, with BANKED passing. The final
status showed BIOS/CCP handoff complete and no trap timeouts or control errors.

The PIO integration test executes the real PIO C driver, clock, trap and bus
code through a BIOS marker and 100 console status/output pairs. The startup
interrupt variant reproduces a clock publication race: the first OUT could
interrupt PIO startup before the configured frequency was recorded, causing
the trap to skip clock resume. Publishing the frequency before starting the
clock fixes that regression; rejected rates restore the previous frequency.

The normal Phase 10 firmware task enables PIO by default. If PIO resources or
banked SRAM are unavailable, firmware falls back to PWM/software I/O.
PIO bank changes briefly extend CLK LOW without invoking ARM; other ports
retain software service. Transfer throughput and electrical timing margins
have not been measured on the board.
