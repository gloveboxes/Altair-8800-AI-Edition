# TICTAC (dcc / CP/M 2.2)

5x5 tic-tac-toe: get four pieces in a row. Arrow keys or Ctrl-E/X/S/D move
the cursor, Space places X, and Q or Escape quits. The CPU plays O.

This is the retained game's dcc port, not the retired BDS C build. It links
the shared client in `../COMMON/DXSVC.C`: countdowns use port 23 and random
tie-breaking uses utility service `0x21` on ports 2/3/4. Input uses nonblocking
CP/M BDOS function 6; output uses direct BIOS `CONOUT`. Old disk-installed BDS
binaries still use retired ports and can
hang before input or at "CPU thinking"; replace them with this build.

With `DCC_DIR` and `NTVCM_DIR` set, run `dccmake` here to produce
`build/TICTAC.COM` without modifying disk images. `build-app.ps1` also installs
the executable on C:, using the shared host builder. Build All discovers this
wrapper automatically. Stop the emulator before updating its disk image.

The original display, scoring, heuristic AI and replay are retained. Human
input polling uses a 25 ms timer; the CPU pauses 400 ms before choosing a move.
Buffered output is flushed before input waits and after visible updates.

The game-local 2048-byte buffer also drains before accepting another byte when
full. It uses the existing dcc `bios(4, byte)` helper rather than stdout or BDOS
functions 2/9, bypassing keyboard look-ahead and Ctrl-S flow control. Left
Arrow/Ctrl-S cannot pause drawing, and drawing does not consume Right
Arrow/Ctrl-D. ANSI sequences, literal `$` and all byte values are preserved;
BIOS output has none of BDOS function 6's reserved input selectors.
Signed decimal scores and cursor/color parameters are formatted locally.

Only the per-byte buffer append helper is `static inline`; flush, string and
number-formatting routines remain out-of-line to limit code growth.
String and digit loops load each byte into a local before calling it so dcc
can inline the append without deferring argument evaluation past a flush.

Draining makes one C/runtime BIOS call per byte rather than one buffered BDOS
function 9 string call. This adds per-byte call and jump-table dispatch overhead
while avoiding BDOS console processing; it is not bulk BIOS output. Timers,
controls, Escape handling, game rules and replay are unchanged. Hardware latency
and throughput have not been measured.

AI score accumulation uses 32-bit `long`: several three-in-a-row lines can
score above the Z80's signed 16-bit `int` range.

## Regression

For focused host tests without creating or installing disk images:

```sh
cc -std=c99 -Wall -Wextra -Werror -Wno-deprecated-non-prototype \
  -fsanitize=address,undefined Apps/TICTAC/tests/game_test.c \
  -o /tmp/tictac-game-test
/tmp/tictac-game-test
rm /tmp/tictac-game-test
```

Run this from the repository root. The tests cover game logic and AI plus
Left/Ctrl-S, Right/Ctrl-D and ANSI keys injected during drawing, all 256 output
byte values, ordering across buffer boundaries, exact ANSI and signed decimal
formatting, replay, Escape timeout, and startup/exit flushing. Host console and
service calls are mocked; these are functional tests, not hardware measurements.

With the MCP server built and the existing host tools installed:

```sh
node Apps/TICTAC/tests/regression.mjs /tmp/tictac-tests /path/to/dcc /path/to/ntvcm
```

This checks the game logic, builds with dcc, and plays human/CPU turns
under the real Z80/CP-M emulator with the shared host service backend.
A bounded Z80 fixture checks two successive turns, the exact timer/random
port protocol, raw BIOS output, keys queued during drawing, arrow input, and
quitting with Q and Escape. All mutable disks
and generated test files stay in the specified directory outside the checkout.
