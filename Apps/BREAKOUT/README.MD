# Breakout

This directory contains a CP/M/Z80 implementation for dcc:

- `breakc11.c`: optimized C11 source for dcc

The C11 version links `../COMMON/DXSVC.C` for framed timer and random-number
services. It uses descriptive C11 identifiers, byte-sized game
state, a precomputed brick-column collision map, and a game-local 4096-byte raw
console buffer. Output is flushed after the initial screen, launch, timed
updates and final screen, or before accepting another byte when the buffer is
full.

Input remains nonblocking BDOS function 6 (`0xff`). Output uses the existing
dcc `bios(4, byte)` helper for direct BIOS `CONOUT`, bypassing BDOS functions
2/9, keyboard look-ahead and Ctrl-S flow control. Left Arrow/Ctrl-S therefore
cannot pause drawing, and drawing does not consume Right Arrow/Ctrl-D.
ANSI sequences, literal `$` and all byte values are preserved; BIOS output has
none of BDOS function 6's reserved input selectors.

Only the per-byte buffer append helper is `static inline`; flush, string and
number-formatting routines remain out-of-line to limit code growth.
String and digit loops load each byte into a local before calling it so dcc
can inline the append without deferring argument evaluation past a flush.

Draining makes one C/runtime BIOS call per byte rather than one BDOS function 9
call per string. This adds per-byte call and jump-table dispatch overhead while
avoiding BDOS console processing; buffering does not make output a bulk BIOS
operation. Ball delays (80/70/60 ms), controls and game rules are unchanged.
Actual device latency and throughput have not been measured.

## Build the C11 version

Set the dcc and ntvcm repository locations, then run the build script:

```powershell
$env:DCC_DIR = "$HOME/GitHub/dcc"
$env:NTVCM_DIR = "$HOME/GitHub/ntvcm"
pwsh ./build-app.ps1
```

The equivalent direct command is:

```sh
dccmake
```

The stripped, peephole-optimized build is written to `build/BREAKOUT.COM` by
`dccmake`; `build-app.ps1` copies it to this directory, removes `build/`, and
installs it into the default C: disk image.

## Controls

- Left/Right arrows: move paddle
- Space or Enter: launch ball
- Escape or Ctrl-C: quit

The host must implement ROMLESS-SVC/1 on ports 2/3/4: timer group `0x10`
(millisecond timer 1) and utility group `0x21` (random operation 0).
With ntvcm, load this repository's Altair I/O library.

## Focused host tests

From this directory:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/console_test.c -o /tmp/breakout-console-test
/tmp/breakout-console-test
rm /tmp/breakout-console-test
```

These tests include the real game source with mocked BIOS/BDOS and service
functions. They inject Left/Ctrl-S and Right/Ctrl-D during drawing, check all
256 output byte values and buffer boundaries, exact ANSI rendering, paddle
movement, launch/quit controls, ball-delay thresholds, and startup/game-over/
exit flushing. They verify functional behavior in a model, not hardware
performance. Use direct `dccmake` for a build without installing a disk image.
