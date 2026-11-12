# Breakout

This directory contains a CP/M/Z80 implementation for dcc:

- `breakc11.c`: optimized C11 source for dcc

The C11 version links `../COMMON/DXSVC.C` for timer and speaker services. It uses descriptive C11 identifiers, byte-sized game
state, a precomputed brick-column collision map, and a game-local 4096-byte raw
console buffer. Output is flushed after the initial screen, launch, paddle movement, sound
toggle, help/startup screens, timed updates and final screen, or before accepting another byte when
the buffer is full.

Input remains nonblocking BDOS function 6 (`0xff`). Output uses the existing
dcc `bios(4, byte)` helper for direct BIOS `CONOUT`, bypassing BDOS functions
2/9, keyboard look-ahead and Ctrl-S flow control. Left Arrow/Ctrl-S therefore
cannot pause drawing, and Right Arrow/Ctrl-D is preserved for the game rather
than consumed by BDOS look-ahead.
Input is also checked every 16 output bytes, including during long redraws.
Those keys enter a 16-byte FIFO and are handled after the current flush, never
by recursively drawing inside an ANSI sequence. A full FIFO stops input reads
until space is available, rather than discarding keys. Each game-loop iteration
handles up to eight pending keys before checking the ball timer, keeping bursts
of arrow repeats responsive while still allowing ball updates.
ANSI sequences, literal `$` and all byte values are preserved; BIOS output has
none of BDOS function 6's reserved input selectors.

The per-byte buffer append and the small one-expression helpers (escape-code
wrappers, timer service calls, and colour lookups) are
`static inline` to avoid call/return overhead. Flush, input FIFO reads,
string, number-formatting, cursor and colour
routines with many call sites remain out-of-line to limit code growth.
String and digit loops load each byte into a local before calling it so dcc
can inline the append without deferring argument evaluation past a flush.

Draining makes one C/runtime BIOS call per byte rather than one BDOS function 9
call per string. This adds per-byte call and jump-table dispatch overhead while
avoiding BDOS console processing; buffering does not make output a bulk BIOS
operation. Sub-cell movement that leaves the displayed ball in the same cell
does not redraw it. Actual device latency and throughput have not been measured.

## Gameplay

The ball uses integer positions in eighths of a terminal cell. Six paddle-hit
zones produce steep centre rebounds and shallow outward edge rebounds, allowing
aimed shots. Both velocity components are bounded to one cell per tick; axial
brick contacts are checked before diagonal corners, so the ball reflects from
the contacted face without skipping bricks or entering their erased slots.

Each ball starts with a 70 ms tick. Four paddle returns reduce it to 60 ms;
twelve reduce it to 55 ms. Hitting an orange brick accelerates to at least the
55 ms pace, and a red brick or the ceiling selects 50 ms. Speed never decreases
during a ball's life. The next timer is armed before movement, sound requests
and rendering, counting that work inside the interval rather than adding it
to the delay. If processing exceeds an interval, the game remains limited by
host/console throughput; it does not perform a burst of catch-up moves.

The paddle moves three columns per key, with immediate visible updates independent
of the ball timer. The paddle stays eight columns wide throughout the game,
including after ceiling hits. A lost ball restores the starting speed and
rally count. Four lives are available.

Paired red, orange, green and yellow brick rows score 7, 5, 3 and 1 respectively.
Paddle returns award no points. Clearing all 104 bricks wins the game, with a
maximum score of 416. These are classic-style mechanics adapted to a terminal,
not an exact reproduction of the original arcade hardware.

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

The startup banner shows a colourful mid-play scene on a black background:
red/orange/green/yellow bricks with a broken tunnel, a cyan ball trail and a
bright magenta paddle. This decorative preview does not change the real wall
or score. It plays a short, nonblocking fanfare when sound is available.
The black backdrop is painted only inside the board with explicit spaces;
screen/status erases use the terminal's default background, preventing coloured
strips outside the board or when starting play.
Space or Enter starts play directly; H opens help before starting.

- Left/Right arrows: move paddle
- Space or Enter: start play / launch ball
- S: toggle sound on/off when a speaker is detected (on by default)
- H: show help and pause play; H, Space or Enter closes help
- Escape or Ctrl-C: quit

Help explains the controls, aiming and difficulty changes. Movement keys are
ignored while help is open; S and quit remain active. Closing help restores
the exact game state and gives the ball a fresh timer interval. Closing help
from the startup banner or a serve prompt does not launch the ball.

The Stage 11 ATtiny85 speaker engine on port 21 supplies the sounds on the
real Z80 SBC. The game detects it at startup; if it is absent or has an
incompatible protocol, the status line shows `S:NO SPEAKER` and play continues
silently. S stops any current sound when muting. The game sends nonblocking
preset requests for the startup fanfare, launch, walls, paddle, bricks, lost balls, game over and
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
input checks during drawing, FIFO ordering/backpressure and bounded input batches,
rebound angles, fractional motion, collision faces/corners, rally and upper-row
acceleration, full-width paddle after ceiling hits/serve reset, scoring, timer-before-render ordering,
help pause/restore and dismissal keys, startup banner/fanfare, and
startup/game-over/exit flushing. They verify functional behavior in a model, not hardware
performance. Use direct `dccmake` for a build without installing a disk image.
