---
name: z80-sound
description: 'Use the DXSVC/DXSDK snd_* sound API in dcc Z80/CP/M apps. Use when adding or debugging game audio, choosing presets or manual effects, playing note sequences, waiting for completion, queueing sounds, setting priorities, or updating playback.'
---

# Z80 sound with DXSVC

Use the SDK helpers; the engine owns playback timing. Sending a sound does not
block the app or require game-side note timing.

## Read these references first

- [DXSVC.H](../../../Apps/COMMON/DXSVC.H): authoritative Doxygen comments for
  every preset and manual effect, argument ranges, repeat rules and contracts.
  Read the description before choosing a sound; do not duplicate the catalogue.
- [SOUND.C](../../../Apps/disk-c-apps/SOUND/SOUND.C): `primitives()` demonstrates
  manual effects; `feedback()` checks priority/queue results; `wait_idle()`
  and `hold()` show responsive waits.
- [BREAKOUT](../../../Apps/disk-c-apps/BREAKOUT/breakc11.c): nonblocking game
  cues, availability checks and a sound toggle.

## Set up and play

Include `"DXSVC.H"`. For apps one folder below a disk-apps directory, add
`../../COMMON/DXSVC.C` to `dcc-input` and `../../COMMON` to
`dcc-include-directory`; preserve the existing dcc toolchain settings.
See [SOUND's build configuration](../../../Apps/disk-c-apps/SOUND/dccmake.txt).
Build with `dccmake` from the app folder.

Call `snd_init()` once: it resets options, stops playback, drains events and
returns 1 if supported. On 0, a game should display "sound unavailable" and
disable audio; a sound-only demo should report the failure.

Minimal finite-sequence demo:

```c
#include <stdio.h>
#include "DXSVC.H"

int main(void)
{
    static unsigned notes[] = {1047, 100, 0, 50, 1568, 200};

    if (!snd_init()) {
        puts("Sound unavailable.");
        return 1;
    }
    snd_sequence(2, 3, 1, notes);
    snd_wait();
    return 0;
}
```

Prefer `snd_preset(priority, SND_...)` for built-ins. For custom notes,
`snd_sequence(priority, count, repeats, notes)` takes 1..8 flat `{Hz, ms}`
pairs in a dcc 16-bit `unsigned` array; 0 Hz is a rest. Count pairs, not words.
`snd_segments` instead takes 1..7 `{target Hz, ms}` glide steps. Consult the
header for other manual effects and limits; do not pass `long` varargs.

## Priority, queue and live options

Priority is 0..255. There is one active sound and a two-slot FIFO, not mixing.

| New play request | Behaviour |
| --- | --- |
| Idle | Starts immediately |
| Playing, queueing off | Higher/equal priority replaces; lower is rejected |
| Playing, `SND_ENQUEUE` set | Queues regardless of priority; rejects if full |

Queued sounds play in arrival order, **not priority order**. Interrupted sounds
do not resume. To interrupt with an urgent cue, turn queueing off first.
`snd_clear()` removes only queued sounds; `snd_stop()` stops playback and clears
the queue.

`snd_options(flags, pitch, speed)` persists for subsequent play requests.
Pitch/speed are 25..200 percent; 100 is normal, higher speed is faster.
Restore the intended options after queueing, as in the demo:

```c
snd_preset(5, SND_ALARM);
snd_options(SND_ENQUEUE, 100, 100);
snd_preset(1, SND_COIN);
snd_preset(1, SND_LASER);
snd_options(0, 100, 100);
```

For a live change, save the original play ID and call
`snd_update(id, mask, pitch, speed, depth_hz, period_ms)`. Masks are
`SND_PITCH`, `SND_SPEED`, `SND_MODULATION` (combine with `|`); only the matching
active sound changes. Options alone do not change existing playback.

## Waiting and game integration

- `snd_wait()` blocks until playback and its queue finish. For a responsive
  wait, poll `snd_busy()` while handling input, as in the demo's `wait_idle()`.
  It returns the active ID or 0 when idle, not a success/completion result.
- In a game, send a cue once per event, gated by availability/enabled state.
  Keep updating the game instead of blocking or retriggering every frame.
- `SND_ENGINE`, `SND_HEARTBEAT`, `SND_LOW_HEALTH` and `SND_TIME_WARNING` repeat
  indefinitely. So do sequences/segments with `repeats == 0` and the manual
  effects whose header documents zero duration as infinite.
  Waiting cannot finish naturally on these, and sounds queued behind them
  cannot start. Use a timer and then stop, or replace with a finite sound;
  remember that stopping also discards the queue.
- Stop when muted or quitting. For a final finite jingle, deliberately let it
  finish before stopping; stopping immediately after play cuts it off.
- For nonblocking completion of a specific request, drain `snd_event()` until
  0; use `SND_EVENT_TYPE(event)` and `SND_EVENT_ID(event)` to match
  `SND_COMPLETED` to the saved play ID. Also handle `SND_INTERRUPTED`,
  `SND_REJECTED`, `SND_STOPPED` and `SND_CANCELLED`, not just completion.
  Only four events are retained; poll regularly and check overflow via
  `status[SND_S_FLAGS] & 4` when relying on events.

## Verify results and troubleshoot

Play/stop/update/clear return a **request ID, not acceptance**. IDs wrap 1..255;
`snd_fx()` returns 0 for an unknown effect. Keep IDs for feedback/live updates.
Declare `unsigned char status[SND_STATUS_BYTES];` then call `snd_read(status)`.
Match `status[SND_S_REQUEST_ID]` to your request before checking
`status[SND_S_RESULT]`, as the demo's `expect()` does:
1 accepted, 2 queued, 3 priority rejection, 4 queue full, 5 no match,
6 updated, 7 stopped, 8 cleared, 9 invalid (0 means no result).
Status words, including completion/rejection counters, are big-endian.

Run the demo and the app in the Apple container using the project's 512 KiB
disks; see [container instructions](../../../docker/README.md). Verify completion, queue order/full,
priority rejection/replacement, mute and cancellation. Unit-test cue selection
with stubs, but use the real SDK/backend to validate playback.
If the demo works but the app is silent, check the deployed binary, init result,
options, request results and early exits. Unrelated services may be unavailable
on a backend (for example, a random-service failure before the first cue);
check that backend's support rather than assuming every SDK call is available.
