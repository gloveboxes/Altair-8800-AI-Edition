# Tetris (dcc C11)

`TETRIS.C` is the 8.3-compatible entry point for `tetris_c11.c`.
The dcc config also links `../COMMON/DXSVC.C`, retaining standard and SDK
include paths. The host must provide ROMLESS-SVC/1 on ports 2/3/4:
millisecond timer 2 in group `0x10` and random words in group `0x21`.

With `DCC_DIR` set and dcc/ntvcm on PATH, `dccmake` creates
`build/TETRIS.COM` without modifying a disk image.
