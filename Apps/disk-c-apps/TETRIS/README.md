# Tetris (dcc C11)

`TETRIS.C` is the 8.3-compatible entry point for `tetris_c11.c`.
The dcc config also links `../COMMON/DXSVC.C`, retaining standard and SDK
include paths. The host must provide ROMLESS-SVC/1 on ports 2/3/4:
millisecond timer 2 in group `0x10` and random words in group `0x21`.

With `DCC_DIR` set and dcc/ntvcm on PATH, `dccmake` creates
`build/TETRIS.COM` without modifying a disk image.

## Raw console

Tetris polls nonblocking input with BDOS function 6 (`0xff`) and renders through
a game-local 4096-byte buffer. Existing visible-update flush points are retained;
a full buffer is also drained before accepting another byte. Output uses the
existing dcc `bios(4, byte)` helper for direct BIOS `CONOUT`, bypassing BDOS
functions 2/9, keyboard look-ahead and Ctrl-S flow control. Left Arrow/Ctrl-S
cannot pause drawing, and drawing does not consume Right Arrow/Ctrl-D.
ANSI sequences, literal `$` and all byte values are preserved; BIOS output has
none of BDOS function 6's reserved input selectors.

Only the per-byte buffer append helper is `static inline`; flush, string and
number-formatting routines remain out-of-line to limit code growth.
String and digit loops load each byte into a local before calling it so dcc
can inline the append without deferring argument evaluation past a flush.

Draining makes one C/runtime BIOS call per byte rather than one buffered BDOS
function 9 string call. This adds per-byte call and jump-table dispatch overhead
while avoiding BDOS console processing; it is not a bulk-output optimization.
The 25 ms frame timer, gravity delays, controls, Escape handling and game rules
are unchanged. Hardware latency and throughput have not been measured.

## Focused host tests

From this directory:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/console_test.c -o /tmp/tetris-console-test
/tmp/tetris-console-test
rm /tmp/tetris-console-test
```

The tests include the real game source with mocked BIOS/BDOS and service
functions. They inject Left/Ctrl-S, Right/Ctrl-D and an ANSI arrow during drawing,
check all 256 output byte values and buffer boundaries, exact ANSI and score
formatting, movement/rotation/drop controls, line scoring, gravity timing,
Escape timeout, and startup/final-screen flushing. These are functional model
tests, not device performance measurements.
