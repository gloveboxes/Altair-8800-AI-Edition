# Hangman

A C11 Hangman game for CP/M 2.2 on the Z80, built with dcc. The game uses a
bordered VT100 interface sized for an 80-by-24 terminal, CP/M direct console
input, and the Altair emulator hardware random-number port.

## Build

Set `DCC_DIR` and `NTVCM_DIR`, then run from this directory:

```sh
"${DCC_DIR}/dccmake"
```

The CP/M executable is written to `build/HANGMAN.COM`.

## Run

```sh
"${NTVCM_DIR}/ntvcm" build/HANGMAN.COM
```

Enter letters from A to Z to uncover the hidden computer-themed word. Press
`Escape`, `Q`, or `Ctrl-C` to quit. After a win or loss, press `Y` to play
again or `N` to exit.
