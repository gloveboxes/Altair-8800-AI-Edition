# Breakout

This directory contains a CP/M/Z80 implementation for dcc:

- `breakc11.c`: optimized C11 source for dcc

The C11 version links `../COMMON/DXSVC.C` for timer and speaker services. It uses descriptive C11 identifiers, byte-sized game
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

The per-byte buffer append and the small one-expression helpers (escape-code
wrappers, timer/key service calls, and colour lookups) are
`static inline`; dcc inlines every call, removing their call/return overhead
for 23 bytes of code. Flush, string, number-formatting, cursor and colour
routines with many call sites remain out-of-line to limit code growth.
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
- S: toggle sound on/off when a speaker is detected (on by default)
- Escape or Ctrl-C: quit

The Stage 11 ATtiny85 speaker engine on port 21 supplies the sounds on the
real Z80 SBC. The game detects it at startup; if it is absent or has an
incompatible protocol, the status line shows `S:NO SPEAKER` and play continues
silently. S stops any current sound when muting. The game sends nonblocking
preset requests for launch, walls, paddle, bricks, lost balls, game over and
victory. More important cues use higher priorities so a wall or paddle bounce
cannot interrupt a brick or lost-ball effect. The final game-over and victory
sounds can finish after the program exits.

The serve direction is randomised locally from input-poll jitter, so no utility
service is needed. The host must implement the independent timer interface on port 23
(millisecond timer 1). Speaker support on port 21 is optional.
With ntvcm, load this repository's Altair I/O library.

## Focused host tests

From this directory:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I ../../COMMON tests/console_test.c -o /tmp/breakout-console-test
/tmp/breakout-console-test
rm /tmp/breakout-console-test
```

These tests include the real game source with mocked BIOS/BDOS and service
functions. They inject Left/Ctrl-S and Right/Ctrl-D during drawing, check all
256 output byte values and buffer boundaries, exact ANSI rendering, paddle
movement, launch/quit controls, sound presets, speaker detection and the S toggle,
ball-delay thresholds, and startup/game-over/
exit flushing. They verify functional behavior in a model, not hardware
performance. Use direct `dccmake` for a build without installing a disk image.
