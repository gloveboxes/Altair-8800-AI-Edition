# TICTAC (dcc / CP/M 2.2)

5x5 tic-tac-toe: get four pieces in a row. Arrow keys or Ctrl-E/X/S/D move
the cursor, Space places X, and Q or Escape quits. The CPU plays O.

This is the retained game's dcc port, not the retired BDS C build. It links
the shared client in `../COMMON/DXSVC.C`: countdowns use port 23 and random
tie-breaking uses utility service `0x21` on ports 2/3/4. Console I/O uses
CP/M BDOS. Old disk-installed BDS binaries still use retired ports and can
hang before input or at "CPU thinking"; replace them with this build.

With `DCC_DIR` and `NTVCM_DIR` set, run `dccmake` here to produce
`build/TICTAC.COM` without modifying disk images. `build-app.ps1` also installs
the executable on C:, using the shared host builder. Build All discovers this
wrapper automatically. Stop the emulator before updating its disk image.

The original display, scoring, heuristic AI and replay are retained. Human
input polling uses a 25 ms timer; the CPU pauses 400 ms before choosing a move.
Buffered output is flushed before input waits and after visible updates.

AI score accumulation uses 32-bit `long`: several three-in-a-row lines can
score above the Z80's signed 16-bit `int` range.

## Regression

With the MCP server built and the existing host tools installed:

```sh
node Apps/TICTAC/tests/regression.mjs /tmp/tictac-tests /path/to/dcc /path/to/ntvcm
```

This checks the game logic, builds with dcc, and plays human/CPU turns
under the real Z80/CP-M emulator with the shared host service backend.
A bounded Z80 fixture checks two successive turns, the exact timer/random
port protocol, arrow input, and quitting with Q and Escape. All mutable disks
and generated test files stay in the specified directory outside the checkout.
