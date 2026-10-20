# Breakout

This directory contains a CP/M/Z80 implementation for dcc:

- `breakc11.c`: optimized C11 source for dcc

The C11 version links `../COMMON/DXSVC.C` for framed timer and random-number
services. It uses descriptive C11 identifiers, byte-sized game
state, a precomputed brick-column collision map, and a 4096-byte fully buffered
console stream. Output is flushed before keyboard polling and after rendered
frames so controls remain responsive.

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

- Left/Right arrows: move paddle
- Space or Enter: launch ball
- Escape or Ctrl-C: quit

The host must implement ROMLESS-SVC/1 on ports 2/3/4: timer group `0x10`
(millisecond timer 1) and utility group `0x21` (random operation 0).
With ntvcm, load this repository's Altair I/O library.
