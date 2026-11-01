# CHESS (dcc / CP/M 2.2)

Human (White) versus CPU (Black) chess, with a colourful 80-column by 30-row
VT100/ANSI display inspired by [TICTAC](../TICTAC/TICTAC.C) and
[Mancala](../MANCALA/manc11.c). ASCII uppercase pieces are White; lowercase
pieces are Black. No Unicode chess font is required.

## Controls

| Key | Action |
| --- | --- |
| Arrows, or Ctrl-E/X/S/D | Move the cyan cursor up/down/left/right |
| Space or Enter | Select a White piece, then move to a dotted legal destination |
| T | Calculate a tip; move from the cursor to the green destination |
| 1 / 2 / 3 | Easy / Medium / Hard (one / two / three plies) |
| N | Start a new game, retaining difficulty and win/loss/draw totals |
| Escape | Cancel a selection; otherwise quit |
| Q or Ctrl-C | Quit |

During promotion, **1 Queen / 2 Rook / 3 Bishop / 4 Knight** replaces the
difficulty controls. Escape cancels promotion without changing the board.
Q and Ctrl-C still quit.

The CPU and tips use the same iterative-deepening alpha-beta **minimax**
engine, with capture/promotion move ordering, material values and positional
bonuses. Leaf positions run a short capture/promotion/check-evasion
quiescence search so the CPU does not stop mid-exchange. Medium is the default.
A search is capped at 12,000 nodes; if the cap is reached, it uses the last
fully completed depth, displayed beside the board.
This is a small Z80 engine, not tournament-strength chess.

Q, Ctrl-C and a lone Escape quit during either search, with the simulated
position restored. Other input during thinking is ignored. ANSI CSI and SS3
arrows are supported; a 120 ms shared-service timer distinguishes lone Escape.
Easy is recommended on slow emulators or hardware.

## Rules

Legal moves cannot expose the king. Castling (including rook movement/capture
rights), en passant, and all four promotions are supported. Check, checkmate
and stalemate are reported.

This casual game automatically draws at threefold repetition, 100 halfmoves
without a pawn move/capture (the fifty-move rule), or recognised insufficient
material. Checkmate takes priority. Repetition uses exact packed positions,
not collision-prone hashes, and ignores en-passant targets that cannot be
legally used. Insufficient-material detection covers bare kings, a lone
bishop/knight, and bishop-only positions with all bishops on one square colour;
it does not incorrectly draw merely because mate cannot be forced.

## Build and run

Set `DCC_DIR` and `NTVCM_DIR` to the local compiler/emulator directories and put
their tools on `PATH`. From this directory:

```sh
dccmake
ntvcm -n -c build/CHESS.COM
```

This creates `build/CHESS.COM` without writing a disk image. Generated `.COM`
files are ignored by Git; rebuild after a fresh checkout.

```powershell
./build-app.ps1 -BuildOnly
```

This collects the executable into this directory without installing it.
After stopping the emulator, `./build-app.ps1` builds and installs
`0:CHESS.COM` on C: (`disks/escape-posix.dsk`). The repository's
`Apps/build-all.ps1` discovers this application automatically. At the CP/M
prompt, switch to C: and run `CHESS`.

Input is nonblocking BDOS 6; rendering uses a game-local 2 KB unsigned-byte
buffer drained through BIOS `CONOUT`, following
[raw console guidance](../../COMMON/RAW-CONSOLE.md). Output does not swallow
arrow keys or trigger Ctrl-S flow control. The only host service used is the
shared Escape timer from [DXSVC](../../COMMON/DXSVC.H).

The engine scores incrementally (make/unmake update the evaluation), skips the
make/unmake legality test for moves that cannot expose the king, and uses
`static inline` helpers. On the Z80 core, a level-3 search costs roughly 11–36M
instructions, 2.5x fewer than before despite the added quiescence search.

All move lists, history and console storage are static and bounded. The build
reserves a 4 KB stack. Scalar indexing and `memcpy` move transfers deliberately
avoid dcc miscompilations of nested member/index expressions and aggregate
array-to-local assignments observed while testing the real Z80 executable.

## Tests

From the repository root:

```sh
node Apps/disk-c-apps/CHESS/tests/regression.mjs \
  /tmp/chess-tests /path/to/dcc /path/to/ntvcm
```

The runner uses an isolated directory and never installs into disk images.
It runs:

- Sanitized native rule/search and UI/raw-console tests.
- Standard perft positions (including start-position depth four: 197,281),
  pins, castling, en passant, underpromotion, draw thresholds and repetition.
- Independent unpruned minimax-plus-quiescence score and chosen-move
  comparisons, and incremental-evaluation consistency checks.
- The dcc-compiled rule/search tests under CP/M in `ntvcm`.
- The actual game binary in the repository Z80 core: levels 1–3, stack usage,
  exact ANSI/numeric output, all byte values, buffer boundaries, drawing-time
  input, human/CPU turns, non-mutating tips, all four keyboard promotions,
  Escape, replay and quit.
- A complete `ntvcm` startup/quit and CP/M memory-budget check.
