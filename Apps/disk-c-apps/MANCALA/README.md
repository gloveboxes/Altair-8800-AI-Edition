# Mancala (dcc C11)

[manc11.c](manc11.c) obtains 16-bit random words through
[DXSVC.C](../../COMMON/DXSVC.C): ROMLESS-SVC/1 utility group `0x21`,
operation 0, on shared ports 2/3/4.

## Controls

- Left/Right arrows or `1`-`6`: select a pit.
- Space or Return: sow the selected pit.
- `T` / `t`: show a tip and highlight the recommended pit. `?` is no longer
  a tip shortcut.
- `U`: undo the last player move.
- `E` / `M` / `H`: Easy / Medium / Hard.
- `Q`, Escape or Ctrl-C: quit.

Tips use a five-ply search and recommend a legal move even in a losing
position. Search scores and tactical bonuses use a common scale to preserve
their relative weights while fitting the Z80's signed 16-bit range and
the search bounds; tactical bonuses are accounted for in root pruning.
Game rules and Easy's random-choice behavior are unchanged.

## Build

With `DCC_DIR` and `NTVCM_DIR` set and dcc/ntvcm on PATH, run from this directory:

```sh
dccmake
```

This produces `build/MANCALA.COM` without modifying a disk image.
The dcc config preserves standard and SDK include paths.

## Regression tests

[tests/tiptest.c](tests/tiptest.c) covers forced losses, all terminal score
margins, minimax selection against an exhaustive oracle on 256 near-endgame
boards, unchanged board/undo state, and `T` / `t` versus `?`.

Run on the host:

```sh
cc -std=c11 -Wall -Wextra -Werror -Wstrict-prototypes \
  -fsanitize=address,undefined -I ../../COMMON tests/tiptest.c -o /tmp/mancala-tiptest
/tmp/mancala-tiptest
```

Run the same tests with actual 16-bit Z80 integers:

```sh
dccmake dcc-input=tests/tiptest.c dcc-output=TIPTEST dcc-build-dir=build-tests
ntvcm build-tests/TIPTEST.COM
```
