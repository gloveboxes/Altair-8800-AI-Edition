# TICTAC (dcc / CP/M 2.2)

5x5 tic-tac-toe: get four pieces in a row. Arrow keys or Ctrl-E/X/S/D move
the cursor, Space places X, T requests a tip, and Q or Escape quits.
The CPU plays O.

Press T (or t) on your turn to highlight a recommended empty square, then
press Space if you want to play it. Tips use the same two-ply minimax engine
from the human's perspective: human move, CPU reply. They do not
place a piece, end your turn or alter the scores. "Finding tip..." appears
during the search, followed by "Tip: highlighted square. SPACE to play."
Moving the cursor or placing a piece clears that message. Repeated tips are
allowed, and equally good suggestions use the shared random-number service.

The CPU's first reply chooses a random empty square in the central 3x3,
excluding the player's piece. Subsequent moves use two-ply minimax and
random tie-breaking. Both use `svc_random_u16()` from the shared service:
on ESP32 this reaches the private utility selector 45 backed by `esp_random()`,
not a software `rand()` sequence. Opening detection uses the board, so replay
automatically enables a fresh random opening without extra state.

After every human or CPU move, the game checks whether either player can
still complete a four-in-a-row line. If every line contains both X and O,
it ends immediately as a draw, displays "No winning moves left.", and
increments the draw score even if empty squares remain. A win takes priority;
a full board without a winner retains the usual draw message. An open line
keeps play going even when neither player can win on their very next move.
The early-draw explanation uses VT100 blink (`ESC[5m`) on terminals that
support it; attributes are reset immediately afterward so other text does not blink.

This is the retained game's dcc port, not the retired BDS C build. It links
the shared client in `../../COMMON/DXSVC.C`: countdowns use port 23 and random
opening selection and tie-breaking use utility service `0x21` on ports 2/3/4. Input uses nonblocking
CP/M BDOS function 6; output uses direct BIOS `CONOUT`. Old disk-installed BDS
binaries still use retired ports and can
hang before input or at "CPU thinking"; replace them with this build.

Function signatures use C11-compatible prototypes supported by dcc. The early
draw check shares the win/search line table and scans only the 28 valid
four-cell windows, stopping as soon as it finds an open line.

With `DCC_DIR` and `NTVCM_DIR` set, run `dccmake` here to produce
`build/TICTAC.COM` without modifying disk images. `build-app.ps1` also installs
the executable on C:, using the shared host builder. Build All discovers this
wrapper automatically. Stop the emulator before updating its disk image.

The original display, scoring and replay are retained. Human
input polling uses a 25 ms timer; the CPU pauses 400 ms before choosing a move.
Buffered output is flushed before input waits and after visible updates.

The game-local 2048-byte buffer also drains before accepting another byte when
full. It uses the existing dcc `bios(4, byte)` helper rather than stdout or BDOS
functions 2/9, bypassing keyboard look-ahead and Ctrl-S flow control. Left
Arrow/Ctrl-S cannot pause drawing, and drawing does not consume Right
Arrow/Ctrl-D. ANSI sequences, literal `$` and all byte values are preserved;
BIOS output has none of BDOS function 6's reserved input selectors.
Signed decimal scores and cursor/color parameters are formatted locally.
The formatter primes the unsigned magnitude after the sign-output branch.
This avoids a dcc peephole loop-registerization bug that left positive values
uninitialized, emitting bogus cursor/color parameters (such as `9873`) and
drawing over the bottom line instead of the board. Replace the disk-installed
`TICTAC.COM` with the rebuilt executable to pick up the fix.

Only the per-byte buffer append helper is `static inline`; flush, string and
number-formatting routines remain out-of-line to limit code growth.
String and digit loops load each byte into a local before calling it so dcc
can inline the append without deferring argument evaluation past a flush.

Draining makes one C/runtime BIOS call per byte rather than one buffered BDOS
function 9 string call. This adds per-byte call and jump-table dispatch overhead
while avoiding BDOS console processing; it is not bulk BIOS output. Timers,
controls, Escape handling, four-in-a-row wins and replay are unchanged. Hardware latency
and throughput have not been measured.

## CPU AI

After the random opening, alpha-beta minimax searches two plies: CPU move,
then human reply. Tips use the same depth with the players reversed.
This reduces Z80 thinking time compared with the previous three-ply search.
Every legal move is considered; move ordering and
alpha-beta cutoffs reduce work without discarding strategically relevant moves.
Equal best root scores are randomized only after resolving cutoff bounds to
exact scores. Terminal wins override the positional evaluation, with faster
wins and delayed losses preferred. Full-board and blocked-line draws score zero.

At the depth limit, each unblocked line contributes 1, 10 or 100 for one, two
or three CPU pieces, with the same weights subtracted for human pieces.
The central 3x3 contributes a 5-point piece bonus (20 at the center), also
signed by player. Nonterminal scores stay within +/-2860, safely below the
30000-point terminal score and within dcc's 16-bit `int`.
The original greedy per-square scoring is no longer used.

Search uses a fixed 25-byte board copy, a fixed move-order buffer, and bounded
recursion with a 1024-byte stack reservation. Simulated moves never alter the
displayed board and are undone before every cutoff. This is depth-limited
minimax, not a full solution of the 5x5 game, so it is not guaranteed unbeatable.
The central-cell lookup avoids division/modulo in the leaf evaluator.
Search time is additional to the 400 ms CPU thinking delay. The regression
fixture measures guest instructions and stack usage, not ESP32 wall-clock timings.
The sparse early-game CPU benchmark dropped from about 4.3 million to 658,000
instructions; an empty-board tip dropped from about 5.2 million to 765,000.
The measured searches use 184 stack bytes for CPU moves and 186 for tips.

## Regression

For focused host tests without creating or installing disk images:

```sh
cc -std=c11 -Wall -Wextra -Werror -Wstrict-prototypes \
  -fsanitize=address,undefined Apps/disk-c-apps/TICTAC/tests/game_test.c \
  -o /tmp/tictac-game-test
/tmp/tictac-game-test
rm /tmp/tictac-game-test
```

Run this from the repository root. The tests cover all 81 piece/empty patterns
on each of the 28 winning lines, every random opening choice for all 25 human
starting squares, unsigned RNG boundary values, two-ply minimax against an
independent unpruned oracle on 32 positions, tactical horizon cases,
alpha-beta bounds, exact random ties and board restoration,
human-side tip decisions against the oracle, T/t input, unchanged board/turn/scores,
playing the suggested square with Space, and clearing/resetting tip feedback,
early draws after human and CPU moves,
win priority, full-board draws, draw scoring and reset, game logic and AI plus
Left/Ctrl-S, Right/Ctrl-D and ANSI keys injected during drawing, all 256 output
byte values, ordering across buffer boundaries, exact ANSI and signed decimal
formatting, replay, Escape timeout, and startup/exit flushing. Host console and
service calls are mocked; these are functional tests, not hardware measurements.

With the MCP server built and the existing host tools installed:

```sh
node Apps/disk-c-apps/TICTAC/tests/regression.mjs /tmp/tictac-tests /path/to/dcc /path/to/ntvcm
```

This checks the game logic, builds with dcc, and plays human/CPU turns
under the real Z80/CP-M emulator with the shared host service backend.
A bounded Z80 fixture checks two successive turns, the exact timer/random
port protocol, raw BIOS output, keys queued during drawing, arrow input, and
quitting with Q and Escape. It also checks exact ANSI and signed decimal output
from the compiled formatter, including full-buffer drains during sign and
escape-sequence output.
The fixture also verifies compiled early draws after both human and CPU moves,
the explanatory message, draw scoring and new-game reset.
Compiled opening tests cover every human starting square, all eligible central
choices and unsigned RNG boundary values through the shared service protocol.
Compiled minimax tests verify the two-ply horizon, immediate-win priority,
blocking a human win, and a sparse early-game move. Each measured search/move
must finish within one million guest instructions and use less than the
reserved 1024-byte stack.
Compiled tip tests check human winning moves, blocking and tactical positions, unchanged
board/turn/scores, cursor highlighting, Space placement, feedback clearing and
new-game reset under the same instruction/stack limits. The real CP/M run
requests both lowercase and uppercase tips before playing the highlighted move.
The real CP/M BIOS run checks startup positioning,
all board cursor coordinates and colors, and initial zero scores rather than
only looking for text. All mutable disks
and generated test files stay in the specified directory outside the checkout.
