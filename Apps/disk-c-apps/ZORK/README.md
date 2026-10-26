# ZORK: The House of Tides

An original Zork-inspired adventure for dcc C11, CP/M 2.2 and the Z80.
Explore **24 rooms**, find **16 objects**, restore the keeper's broken
machinery and recover the house's three treasures. The main quest is to
place the **jade idol, silver chalice and star crown** in the cabinet west
of the house. An optional ruby and the keeper's unfinished story reward
further exploration. The maximum score is **300**.

On launch, a framed introduction describes the forgotten coast, the vanished
keeper, the inhabitants of the house and your quest. Press **Enter** to begin,
or **Q** / **Ctrl-C** to return to CP/M. Reading the introduction does not
consume turns or lamp charge. `RESTART` starts a fresh game directly.

## The adventure

The house connects a library and crypt, a ruined greenhouse and well,
underground waterworks, a clockwork bridge, a mine and a star observatory.
The routes meet at the reservoir, so the warden's bridge has another approach.
The upper and lower parts of the house share machinery and clues:

- The crypt's coin slot and the gallery's brass lock lead to the jade idol.
- Repairing the pump and opening its valve drains the shrine's causeway,
  revealing a path to the chalice and powering the tide engine.
- Restoring the tower's clock, fitting a telescope lens and running the
  waterworks open the observatory. Its star case asks a spoken riddle.
- Returning a journal to the keeper's ghost restores its memories and earns
  a flute. Read the journal before handing it over for more of the story.
- The clockwork warden patrols, becomes alert, sleeps briefly to music, or
  permanently befriends a visitor who oils its joints. Lingering while it
  is alert causes a safe escort to the gallery. It never takes your items.
- A hungry raven, a bolted gear case and an unsafe tower ladder provide
  further object puzzles. The mine holds an optional treasure.

`JOURNAL` tracks the main branches and the ghost's request without requiring
an item. `HINT` gives a clue for your current puzzle or the next unfinished
branch. Examine scenery, read notes and talk to the characters to discover
solutions in the world. NPC behaviour and all puzzle solutions are deterministic.

## Terminal interface

Use a VT100/ANSI terminal with at least **80 columns and 30 rows**, matching
Breakout's display size. The game retains its grey/white checker border,
coloured headings and buffered BIOS `CONOUT` output.

The status line shows score, turns and remaining lamp charge. The upper
panel shows the current room, changing scenery, characters, visible objects
and blocked exits. The lower panel has a ten-line adventure log and a bounded
command editor. Prose and object lists wrap inside the frame.

Input uses direct BDOS function 6. Backspace/DEL edits the command; Ctrl-U or
Ctrl-X clears it. ANSI arrow sequences are ignored. Ctrl-C or `QUIT` exits
normally. Commands are limited to 70 characters.

Left Arrow maps to Ctrl-S (`0x13`) and Right Arrow to Ctrl-D (`0x04`) in this
project's keyboard path. The editor ignores these bytes. All game rendering,
including startup and cleanup, uses BIOS `CONOUT` to bypass CP/M's BDOS 2/9
output flow control. Wrapping is disabled during play; normal wrapping,
colours and the visible cursor are restored on exit.

## Build and run

Set `DCC_DIR` and `NTVCM_DIR`, then run from this directory:

```sh
"${DCC_DIR}/dccmake"
"${NTVCM_DIR}/ntvcm" -c build/ZORK.COM
```

`-c` prevents ntvcm from switching to its automatic 80-by-24 display mode.
The build writes `build/ZORK.COM` without installing it into a disk image.

The game links `../../COMMON/DXSVC.C`. An optional portrait event requests
ROMLESS-SVC/1 utility group `0x21`, random operation 0, on ports 2/3/4 when
entering the gallery. A failed or short response is ignored; all quests work
without a random service. No real-time timers are required.

## Commands

Commands are case insensitive and match whole words. Articles and common
adjectives work: `TAKE THE BRASS KEY`, for example.

| Action | Examples |
| --- | --- |
| Move | `N`, `E`, `S`, `W`, `U`, `D`, `GO NORTH`, `CLIMB UP` |
| Look and read | `LOOK`, `LOOK AT CLOCK`, `EXAMINE SLOT`, `X NOTE`, `READ JOURNAL` |
| Find objects | `TAKE KEY`, `GET KEY`, `TAKE ALL`, `DROP KEY`, `INVENTORY`, `I` |
| Use objects | `USE KEY ON DOOR`, `USE ROPE`, `USE LENS ON MOUNT` |
| Give and feed | `GIVE JOURNAL TO GHOST`, `GIVE BREAD TO RAVEN`, `FEED RAVEN` |
| Mechanisms | `OPEN CASE`, `UNLOCK VAULT`, `REPAIR PUMP`, `TURN VALVE`, `WIND CLOCK` |
| Characters | `TALK TO GHOST`, `TALK WARDEN`, `PLAY FLUTE` |
| Light | `LIGHT LAMP`, `EXTINGUISH LAMP`, `USE LAMP` to toggle, `USE BATTERY` |
| Treasure | `PUT IDOL IN CABINET`, `DEPOSIT GEM`, `DEPOSIT ALL` |
| Guidance | `JOURNAL`, `HINT`, `HELP`, `SCORE` |
| Other | `SAY DAWN`, `WAIT`, `RESTART`, `QUIT`, `Q` |

`USE` and `GIVE` can infer the nearby target when it is unambiguous. `OPEN`
alone operates the nearby passage or case. `REPAIR CLOCK` fits the gear or
applies oil as needed; the clock must then be wound.

## Time, light and recovery

Only successful actions and `WAIT` advance time. Looking, talking, reading,
help, journal and hints are free. Failed actions and blocked movement do not
advance time. The warden gives you two further successful turns after it
becomes alert before escorting you out. Playing the flute grants four turns
of sleep, including the action that plays it.

The lamp begins in the hall with **160 lit turns**. The pantry's spare battery
adds **120 turns**, once, and works even after the lamp has run completely
out. Switching the lamp off preserves its remaining charge; switching it
back on does not refill it. A lit lamp illuminates only its own room or the
room of the person carrying it. You can still follow directions in darkness
to retreat or recover supplies. `RESTART` resets the whole adventure.

Useful objects have no carrying limit and can be dropped and recovered.
The coin, bread, rope, gear, lens, journal and battery are committed to their
correct puzzle when used. The wrench, key, oil and flute can be reused.
Treasures remain safe in the cabinet once deposited.

The game ends when all three main treasures are banked. Complete optional
tasks and deposit the mine gem before banking the last main treasure if you
want all 300 points. Returning the journal also changes the ending.

## Scoring

| Achievement | Maximum points |
| --- | ---: |
| Discover all 24 rooms in light | 24 |
| First collection of all 16 objects | 16 |
| Puzzle solutions and character quests | 135 |
| Bank the idol, chalice, crown and optional gem | 105 |
| Complete the three-treasure quest | 20 |
| **Total** | **300** |

Each award can be earned once. Repeated unlocking, maintenance, collecting,
feeding or treasure deposits cannot generate additional points.

<details>
<summary>Full 300-point walkthrough</summary>

The command list is in [tests/walkthrough.txt](tests/walkthrough.txt). It
starts after pressing Enter on the introduction screen. It
visits every room, collects every object, resolves the keeper's story,
befriends the warden and banks all four treasures. It includes the optional
branches, so it is longer than a minimum winning route. Press Enter at the
victory screen to return to CP/M.

</details>

## Regression checks

From the repository root:

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  Apps/disk-c-apps/ZORK/tests/game_test.c -o /tmp/zork-game-test
/tmp/zork-game-test
```

Tests include the real source with mocked BIOS, BDOS and shared services.
They play the complete 300-point quest and cover hidden items, puzzle gates,
repeated rewards, wrong targets, parser collisions, battery recovery, ghost
rewards, warden alert/sleep/friendly states and safe retreat. They also check
reciprocal map links, description lengths and all sixteen dropped objects
against the fixed panel dimensions.

The console checks inject Left/Ctrl-S and Right/Ctrl-D during startup,
buffer drains and later redraws. They require output to finish and both
keys to reach direct input in order. A screen model checks border preservation,
command editing and cleanup. Intro checks cover paragraph layout, Enter to
begin, Q/Ctrl-C exits and zero turn or lamp-charge use. Mocks reject BDOS
output calls.

After building, play the real Z80 binary, including failed/repeated actions,
a full win, the warden's musical sleep and escort, and both exit paths:

```sh
python3 Apps/disk-c-apps/ZORK/tests/target_test.py "${NTVCM_DIR}/ntvcm" \
  Apps/disk-c-apps/ZORK/build/ZORK.COM
```

This also checks Ctrl-S/Ctrl-D queued through startup. It pipes keystrokes to
ntvcm in a temporary directory without changing disk images. These are
functional and emulated execution checks, not hardware performance measurements.
