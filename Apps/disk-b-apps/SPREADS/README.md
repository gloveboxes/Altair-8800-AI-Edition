# SPREADS

This small DCC application copies two Z80 routines into one bank-1 allocation.
The call sites look like ordinary C functions: `vegemite(&params)` and
`marmite(&params)`. Their small bank-0 wrappers write the C parameter block
into a bank-1 allocation with `bwrite`, call the selected entry point through
BIOS service operation 5 (CALL), and copy the result back with `bread`. The bank-1
routines read `requested` through that pointer, write `written` and `text`,
and return `written` in HL. The base-bank pointer `&params` is never
dereferenced while bank 1 is selected.

`spreads_load()` checks the ABI, allocates the bank-1 code, stack and parameter
block, and copies the code. `spreads_unload()` frees those allocations, keeping
that setup and cleanup out of `main`.

Entry offset 0 writes `VEGEMITE`; the second entry writes `MARMITE`. Calls
request 8 and 7 characters respectively, followed by a 3-character request
that produces `VEG`. This last call shows that the input value, not just the
entry point, affects the result.

The two routines have no absolute references, so this example needs no code
relocation. It allocates separate code, stack and context blocks, then frees
all three. It needs BIOS ABI 1.0, call revision 1.

Build from this directory with the local DCC toolchain:

```sh
DCC_DIR=/path/to/dcc /path/to/dcc/dccmake
```

`dccmake.txt` reuses the shared [BANKSDK](../BANKSDK/README.md) wrappers and produces
`build/SPREADS.COM`. Copy that COM to a CP/M drive and run `SPREADS`.
