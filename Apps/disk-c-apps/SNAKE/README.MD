# Snake

`snakec11.c` is the dcc C11 version. Its `dccmake.txt` links
`../COMMON/DXSVC.C`, preserving both the dcc standard headers and SDK include
directories. The host must provide ROMLESS-SVC/1 on ports 2/3/4: millisecond
timer 2 in group `0x10` and random words in group `0x21`.

Run `dccmake` from this directory with the dcc and ntvcm directories on PATH
and `DCC_DIR` set to the dcc checkout. This produces `build/SNAKE.COM`
without modifying a disk image.

## Raw console

Snake polls nonblocking input with BDOS function 6 (`0xff`) and renders through
a game-local 4096-byte buffer. Complete visible updates flush the buffer; a
full buffer is also drained before accepting another byte. Output uses the
existing dcc `bios(4, byte)` helper (BIOS `CONOUT`), not stdout or BDOS functions
2/9. Drawing therefore does not invoke BDOS keyboard look-ahead or Ctrl-S flow
control. ANSI sequences, `$`, NUL and high bytes are passed unchanged; unlike
BDOS function 6 output, BIOS `CONOUT` has no reserved input-selector bytes.

The per-byte buffer append, small rendering wrappers, timer/random wrappers
and direction/delay predicates use `static inline`. Flush, string and
number-formatting routines remain out-of-line to limit code growth. dcc may
retain a private fallback body when a call cannot be safely inlined.
String and digit loops load each byte into a local before appending it so dcc
can inline the append without deferring argument evaluation past a flush.
`read_key` remains out-of-line: inlining it slightly increased the instruction
count of empty input polls in the tested build.

Buffering batches rendering, but draining still makes one C/runtime BIOS call
per byte. Compared with a buffered BDOS function 9 string call, this adds
per-byte call, loop and BIOS jump-table dispatch overhead, while avoiding BDOS
console processing. It is not a bulk-output optimization. The 20 ms timer,
movement delays and flush points are unchanged; actual frame time still
includes synchronous drawing and has not been measured on hardware.

## Incremental VT100 rendering

Only the old tail, old head and new head need painting during an ordinary
move; the board and status are not redrawn. The renderer additionally:

- Retains foreground/background state and skips unchanged colour settings and
  redundant resets. Colours persist between movement updates; status and final
  messages explicitly restore default attributes before drawing text.
- Paints horizontal head/body neighbours left-to-right, avoiding a second
  cursor-position sequence.
- Skips clearing a departing tail cell when the new head immediately replaces it.
- Formats screen coordinates with a two-digit lookup instead of general
  division/modulo. All current screen coordinates are below 100.

For ordinary moves with two-digit screen coordinates, output falls from
49 to 37 bytes horizontally (24.5% fewer) and 45 vertically (8.2% fewer).
These byte budgets are regression-tested alongside the resulting screen.

A bounded run of the compiled program using the repository's Z80 core measured
44,981 versus 31,233 guest instructions for eight moves (two per direction,
including buffer drains), a 30.6% reduction. BIOS `CONOUT` was intercepted, so
this is not a measurement of terminal or ESP32 latency. Exhaustively formatting
row/column pairs 1–99 took 6,251,256 versus 2,927,826 guest instructions,
excluding drains.

With the same optimized renderer and the original inline byte writer retained,
inlining the additional helpers reduced the eight-move workload from 31,805 to
31,233 instructions (1.8%), costing 128 bytes. The complete optimized build grew
from 6,912 to 7,680 bytes. These are measurements of the tested dcc build, not
guarantees for other compiler revisions.

## Focused host tests

From this directory, using the host C compiler:

```sh
cc -std=c11 -Wall -Wextra -Werror -I../../COMMON -fsanitize=address,undefined \
  tests/console_test.c -o /tmp/snake-console-test
/tmp/snake-console-test
rm /tmp/snake-console-test
```

The tests include the real game source with mocked BIOS/BDOS and service
functions. They inject movement keys during output, including Left/Ctrl-S,
Right/Ctrl-D and an ANSI arrow, and verify that only the game's raw input
polls consume them. They also check all 256 byte values, ordering across
buffer boundaries, empty/repeated flushes, every screen-coordinate pair from
1–99, rendition-cache resets, exact ANSI rendering and movement byte budgets.
A terminal model checks every board cell after successive moves in all four
directions, growth, score/speed updates and moving into the departing tail.
Escape timeout, timer settings and startup/exit flushing are also covered.
These are functional model tests, not measurements of device latency or
throughput.