# Breakout

This directory contains two CP/M/Z80 implementations:

- `BREAKOUT.C`: original BDS C 1.6 source
- `breakc11.c`: optimized C11 source for dcc

The C11 version is self-contained and talks directly to the Altair timer and
random-number ports. It uses descriptive C11 identifiers, byte-sized game
state, a precomputed brick-column collision map, and a 4096-byte fully buffered
console stream. Output is flushed before keyboard polling and after rendered
frames so controls remain responsive.

## Build the C11 version

Set the dcc and ntvcm repository locations, then run the build script:

```sh
export DCC_DIR="$HOME/GitHub/dcc"
export NTVCM_DIR="$HOME/GitHub/ntvcm"
./build-app.sh
```

The equivalent direct command is:

```sh
dccmake
```

The stripped, peephole-optimized build is written to `build/BREAKOUT.COM` by
`dccmake`; `build-app.sh` copies it to this directory and removes `build/`.

## Controls

- Left/Right arrows: move paddle
- Space or Enter: launch ball
- Escape or Ctrl-C: quit

The host emulator must provide the Altair timer ports 26/27 and random-number
ports 45/200. With ntvcm, load this repository's Altair I/O library.
