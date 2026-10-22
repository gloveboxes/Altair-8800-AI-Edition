# Lessons learned: Ctrl-S, gameplay and raw console buffering

## The failure: output can consume input

Snake, Breakout, Tetris and TicTac already polled the keyboard through
`bdos(6, 0xff)`, but rendered through buffered C stdout. That combination was
not raw console I/O end to end.

The dcc runtime used during this investigation drains stdout through CP/M BDOS
functions 9 (string output) and 2 (character output). These normal-console
paths check keyboard input and implement Ctrl-S flow control. Consequently:

- Left Arrow, mapped to Ctrl-S (`0x13`) by this project's keyboard path, could
  pause output when it arrived during drawing.
- Other movement input, including Right Arrow/Ctrl-D (`0x04`), could enter
  BDOS's normal-console look-ahead buffer instead of reaching the game's
  subsequent direct-input poll.
- The symptoms could look like a frozen game, slow rendering or missed keys,
  even though the game's input decoder and timers were correct.

**Raw input alone is insufficient. Trace the output path as well.** Making
stdout's buffer larger, flushing it more often or making it unbuffered does
not fix which BDOS console functions it eventually calls.

These control codes describe this project's keyboard mapping, not a universal
encoding for terminal arrows. Games that accept ANSI arrows must also retain
their existing multi-byte decoder and lone-Escape timeout.

## Recommended approach for new dcc games

1. Keep nonblocking input on `bdos(6, 0xff)`.
2. Give the game a fixed-size, static, unsigned-byte output buffer.
3. Drain it through the existing dcc `bios(4, byte)` helper, which calls BIOS
   `CONOUT` directly, bypassing BDOS normal-console look-ahead and flow control.
4. Append bytes without calling the BIOS until a visible update is complete
   or another byte needs space in a full buffer.
5. Flush after startup rendering, movement/status updates, prompts before
   input waits, game-over/replay screens and terminal cleanup before exit.
6. Keep the buffer, formatting and flush decisions game-local. Do not change
   the global runtime, CP/M, firmware or keyboard mappings to fix rendering.

The adopted capacities are 4096 bytes for Snake, Breakout and Tetris, and
2048 bytes for TicTac. These are examples, not mandatory values: select a
bounded capacity that fits the game's CP/M memory budget. Do not put a
multi-kilobyte buffer on the stack. A screen larger than the buffer must still
work by draining full chunks and then the final partial chunk.

### Reference pattern

Use this pattern inside the game's source, not as a global stdout replacement:

```c
#include <stdint.h>
#include <stdlib.h>

#define CONSOLE_BUFFER_SIZE 4096

static uint8_t console_buffer[CONSOLE_BUFFER_SIZE];
static unsigned console_buffer_used;

static void console_flush(void)
{
    unsigned index;

    for (index = 0; index < console_buffer_used; ++index)
        bios(4, console_buffer[index]);
    console_buffer_used = 0;
}

static inline void console_put_byte(uint8_t byte)
{
    if (console_buffer_used == CONSOLE_BUFFER_SIZE)
        console_flush();
    console_buffer[console_buffer_used++] = byte;
}

static void console_write_text(const char *text)
{
    uint8_t byte;

    while (*text != '\0') {
        byte = (unsigned char)*text++;
        console_put_byte(byte);
    }
}

static int read_key(void)
{
    return bdos(6, 0xff) & 0xff;
}
```

This maintains `0 <= console_buffer_used <= CONSOLE_BUFFER_SIZE`. A full
buffer is drained **before** another byte is stored, so no bytes are dropped
and the array is never overrun. An empty or repeated flush emits nothing.
BIOS `CONOUT` has no defined write-error status to check; this pattern assumes
the synchronous CP/M BIOS console contract.

The text helper stops at NUL. To output NUL or arbitrary binary byte values,
use the byte helper with an explicit count instead. Do not add a `$`
terminator or translate ANSI, CR/LF or high bytes. A full-buffer drain may
split an ANSI sequence; sequential drains preserve the same ordered byte
stream. Still flush the final partial buffer at the visible-update boundary.

The writer is single-owner and non-reentrant. Do not call it from an interrupt
handler or recursively from its output backend.

### Why BIOS output rather than BDOS function 6 output?

BDOS function 6 also provides direct output, but `0xff` selects input rather
than emitting that byte. It is not an arbitrary-byte output interface.
Never work around this by masking bytes to seven bits or silently dropping
reserved values. BIOS `CONOUT` has no such selector ambiguity: `$`, NUL,
`0xff` and ANSI bytes are passed as data.

Use the existing `bios(4, byte)` helper rather than hardcoding the BIOS address
or adding a new assembly bridge. The helper locates the BIOS jump table and
passes the output byte in the BIOS-required C register.

Raw BIOS output avoids the CP/M-side problem; it does not override a host
terminal's own software flow control or guarantee infinite keyboard buffering.
If an external terminal still pauses on Ctrl-S, investigate that separate
layer rather than changing the game's controls.

## Keep every normal rendering path raw

Replace or route all game-owned rendering through the local writer, including
cursor/color helpers, text, numbers, borders, startup, replay and exit. Leaving
one `printf`, `putchar`, `puts` or `fputs` on stdout can reintroduce the
normal-console path. `fflush(stdout)` does not drain the new local buffer;
call the game's flush function instead.

Use small local integer formatters where needed. Preserve signedness, zero,
16-bit limits and 32-bit `long` scores; do not truncate a game's existing
score range just to remove `printf`.

Shared service diagnostics are a separate error-reporting path and may still
use stdout. Do not silently suppress errors or claim that such diagnostics
have become raw. Keep this fix scoped to normal game rendering, and investigate
fatal diagnostic stalls separately if they occur.

## Inlining lessons for dcc

The per-byte append is the hot, small helper worth trying as `static inline`.
Keep the flush loop, text loops and numeric formatters out-of-line initially
to avoid unnecessary code expansion.

During these changes, merely adding `inline` removed literal-byte calls but
left calls in the string and digit loops. Loading each byte into a local
before calling the append helper allowed the installed dcc compiler to inline
those paths too:

```c
while (count != 0) {
    byte = digits[--count];
    console_put_byte(byte);
}
```

This also makes evaluation explicit: read the byte once before a possible
flush. Prefer that to relying on a compiler to inline an argument such as
`*text++` or `digits[--count]` across the conditional flush.

Do not assume the keyword guarantees optimization. Inspect generated Z80
assembly for remaining calls and compare optimized `.COM` sizes. In the
2026-10-22 builds, the append function and its calls disappeared in all four
games; total binary sizes grew by 384 bytes for Snake/Tetris and 512 bytes
for Breakout/TicTac. These are build observations, not measured speedups or
guarantees for another compiler revision.

## CPU overhead and timing

The local buffer batches rendering decisions, not BIOS output: draining still
makes one C/runtime BIOS call per byte. Compared with buffered BDOS function 9
string output, that adds per-byte call, loop and BIOS jump-table dispatch
overhead while avoiding BDOS console processing. Inlining the append removes
its call overhead, not the BIOS dispatch cost.

Flushes are synchronous, so drawing still takes time and the game does not
poll input while draining. Reduce unnecessary redraws rather than changing
timer values to hide output costs. Preserve timer placement and movement,
gravity or CPU-think delays during a transport-only fix. The user reported
improved Snake gameplay; hardware latency and throughput were not measured.

## Validation checklist

- Build using the existing game configuration. Use direct `dccmake` for
  build-only validation; install wrappers may modify deployment disk images.
- Inject Left/Ctrl-S and Right/Ctrl-D **during drawing**, not only before it.
  Verify output completes and that subsequent game polls consume both keys.
- Exercise ANSI arrows, lone Escape, launch/drop/place and quit controls as
  applicable, without changing decoder timing.
- Assert that normal game output uses BIOS function 4, and that game input
  uses only BDOS function 6 with `0xff`. Reject BDOS 2/9 output in mocks.
- Check byte-for-byte ANSI output and all 256 byte values, especially `$`,
  NUL and `0xff`.
- Check empty/repeated flushes and lengths 1, capacity-1, capacity, capacity+1
  and multiple capacities plus a remainder. Check strings and decimal digits
  crossing a full-buffer boundary, not just direct byte appends.
- Check initial/final screens, replay, scores, game rules and timer settings.
- Run host tests with bounds/undefined-behavior sanitizers. Host mocks test
  the contract, not the target ABI: also build the target and use a bounded
  Z80 fixture where available.
- Distinguish functional tests, emulated execution, binary-size comparisons
  and real hardware performance measurements in the results.

## Working examples and regression tests

- [Snake writer](../SNAKE/snakec11.c) and
  [console tests](../SNAKE/tests/console_test.c).
- [Breakout writer](../BREAKOUT/breakc11.c) and
  [console tests](../BREAKOUT/tests/console_test.c).
- [Tetris writer](../TETRIS/tetris_c11.c) and
  [console tests](../TETRIS/tests/console_test.c).
- [TicTac writer](../TICTAC/TICTAC.C),
  [game/console tests](../TICTAC/tests/game_test.c) and
  [bounded Z80 fixture](../TICTAC/tests/z80_game_fixture.cpp).
