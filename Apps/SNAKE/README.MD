# Snake

`snakec11.c` is the dcc C11 version. Its `dccmake.txt` links
`../COMMON/DXSVC.C`, preserving both the dcc standard headers and SDK include
directories. The host must provide ROMLESS-SVC/1 on ports 2/3/4: millisecond
timer 2 in group `0x10` and random words in group `0x21`.

Run `dccmake` from this directory with the dcc and ntvcm directories on PATH
and `DCC_DIR` set to the dcc checkout. This produces `build/SNAKE.COM`
without modifying a disk image.