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

Only the per-byte buffer append helper is `static inline`; flush, string and
number-formatting routines remain out-of-line to limit code growth.
String and digit loops load each byte into a local before calling it so dcc
can inline the append without deferring argument evaluation past a flush.

Buffering batches rendering, but draining still makes one C/runtime BIOS call
per byte. Compared with a buffered BDOS function 9 string call, this adds
per-byte call, loop and BIOS jump-table dispatch overhead, while avoiding BDOS
console processing. It is not a bulk-output optimization. The 20 ms timer,
movement delays and flush points are unchanged; actual frame time still
includes synchronous drawing and has not been measured on hardware.

## Focused host tests

From this directory, using the host C compiler:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/console_test.c -o /tmp/snake-console-test
/tmp/snake-console-test
rm /tmp/snake-console-test
```

The tests include the real game source with mocked BIOS/BDOS and service
functions. They inject movement keys during output, including Left/Ctrl-S,
Right/Ctrl-D and an ANSI arrow, and verify that only the game's raw input
polls consume them. They also check all 256 byte values, ordering across
buffer boundaries, empty/repeated flushes, exact ANSI rendering, movement
output, Escape timeout, timer settings and startup/exit flushing. These are
functional model tests, not measurements of device latency or throughput.