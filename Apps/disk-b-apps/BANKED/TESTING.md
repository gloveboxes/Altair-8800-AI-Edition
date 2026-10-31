# BANKED verification

## Extent allocator

The general pool is `C800h–DDFFh` (5,632 bytes, 22 extents of 256 bytes). It
uses separate extent-length, physical-predecessor and doubly linked free-list
tables. All pool operations execute in bank 1 on a private assembly stack.

`banked_allocator_z80_test.py` executes the resident gateway, dispatcher,
allocator, context hooks and library-control code as Z80 code. It covers
allocation rounding, exhaustion, invalid and interior handles for every page,
double free, reset epochs, coalescing, pinned library/stack handles, the
capability record, distinct status codes, library-request validation,
DMA/search context restoration, byte transfers and per-operation T-state
budgets. ALLOCPRF measures allocation/free pairs with empty, occupied-prefix
and fragmented pools.

## Endurance application

The default `build/BANKED.COM` is built with the local DCC runtime and a
512-byte stack. It uses stopwatch 0 on timer port `0x17` to run for 43,200
seconds; CP/M's calendar clock is not required. Each iteration writes,
reads back and compares the whole pool with 24 patterns: zero, ones,
alternating, address-derived and inverse values, and walking one/zero bits.
The allocator check covers rounding, small and large allocations,
end-of-block transfers, fragmentation, reuse, complete exhaustion and the
final pool byte.

For a finite emulator run, build one complete 24-pattern cycle into a
separate directory:

```sh
cd examples/BANKED
DCC_DIR=/path/to/dcc "$DCC_DIR/dccmake" \
  dcc-define=BANKED_ONE_CYCLE dcc-build-dir=build/emulator
```

`banked_heap_z80_test.py` boots the production CP/M image with the bank-0
BDOS shim, mirrored gateway and complete bank-1 resident image, then runs the
COM file. Only console, disk and the stopwatch port are modeled. It checks
all 24 patterns over 5,632 bytes, full pool recovery, PRIMEBK1 and SPREADS
direct calls, the unavailable-bank path, timer parser recovery from
incomplete commands and errors, and simulated 12-hour expiry without falsely
counting an incomplete iteration. MEMPERF and ALLOCPRF tests use the same
fixture.

## Gateway

`banked_memory_z80_test.py` covers INFO and the `BMX1` descriptor, rejection
of misaligned counts, overflow and resident-page access, maximum-length
65,024-byte transfers at unaligned and boundary addresses in both
directions and interrupt states, register and stack preservation, and
per-size cycle budgets. The resident page comes from the validated CP/M boot
package; firmware mirrors all 256 bytes into bank 1 and verifies them.

## Commands

From the Z80ROMlessSBC repository root:

```sh
export PYTHONPATH=src/common/tests DCC_DIR=/path/to/dcc
build/disk-emulator/bin/python -m unittest \
  banked_memory_z80_test banked_allocator_z80_test bank1_bdos_z80_test \
  bank0_bdos_shim_z80_test bank1_system_boot_z80_test bank_sdk_z80_test \
  banked_heap_z80_test memperf_z80_test allocperf_z80_test
python3 -m unittest discover -s src/cpm -p 'test_*.py'
```

## PIO backend and board

The PIO-specific host tests exercise the real PIO C driver through an
instrumented HAL and execute the assembled 31-word PIO program in a digital
model. They cover allocation fallback, nested interrupt masking, slow-IN data
pin turnaround, bank preservation across bus grants, timeouts, every D0–D7
value at six clock phases, port aliases, slow-service routing, BUSACK gating
and a full progress FIFO. The model checks minimum clock pulse lengths and
the eight-tick WAIT-release guard. The Z80 emulator tests do not emulate
RP2350 peripherals.

The PIO integration test executes the real PIO C driver, clock, trap and bus
code through a BIOS marker and 100 console status/output pairs. Its startup
interrupt variant checks that the configured frequency is published before
the clock starts, so the first OUT always resumes the clock.

Phase 10 firmware enables PIO by default. If PIO resources or banked SRAM
are unavailable, firmware falls back to PWM/software I/O. PIO bank changes
briefly extend CLK LOW without invoking ARM; other ports use software
service. Emulator results are not hardware qualification: rerun BANKED on
the board after deploying matching firmware and CP/M system image.
