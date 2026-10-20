# Mancala MANC11 (dcc C11)

The retained `manc11.c` obtains 16-bit random words through
`../COMMON/DXSVC.C`: ROMLESS-SVC/1 utility group `0x21`, operation 0, on
shared ports 2/3/4. Game rules, controls and random-choice behavior are unchanged.

With `DCC_DIR` set and dcc/ntvcm on PATH:

```sh
dccmake
```

This produces `build/MANC11.COM` without modifying a disk image.
The dcc config preserves standard and SDK include paths.
