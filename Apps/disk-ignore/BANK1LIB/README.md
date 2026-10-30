# Fixed-address DCC bank-1 library

The declarative [interface](interface.json) lists the bank-1 export name,
linked C symbol, bank-0 function name, and argument shape. `build_library.py`
passes those exports to `l80cc` and generates [SPAPI.H](SPAPI.H),
[SPAPI.C](SPAPI.C), and [LIBAPI.H](LIBAPI.H). The two headers declare the
bank-0 wrappers and bank-1 functions from the same contract.

`HOST.C` calls `spopen()`, `veg(&value)`, `marm(&value)` and `spclos()` as normal
C functions. The generated wrappers load `SPREADS.LIB` at bank-1 `0100h`,
resolve each entry **by name**, and use the BIOS direct-offset call. For
`inout_u16`, a wrapper allocates a bank-1 parameter block, copies the value
in and out, and frees the block. It invokes the exported onload/onunload
functions around the module lifetime. The bank-1 C library exercises DCC
`malloc`, console output and file I/O.

The library build writes `SPREADS.LIB` beside `LIB.C`; intermediate assembly
and object files stay under `build/`.

Build from the repository root:

```sh
DCC_DIR=/path/to/dcc python3 Apps/disk-b-apps/BANK1LIB/build_library.py
cd Apps/disk-b-apps/BANK1LIB
DCC_DIR=/path/to/dcc /path/to/dcc/dccmake
```

The outputs are `Apps/disk-b-apps/BANK1LIB/SPREADS.LIB` and
`Apps/disk-b-apps/BANK1LIB/build/LIBHOST.COM`. Copy both to a CP/M disk. This example
requires BIOS ABI 2.5 and allocator revision 5.

`LIB/dccmake.txt` points `l80c-tool` to the repository-root `l80cc`, but the
current DCC `dccmake` links `.COM` programs. Use `build_library.py` to produce
the fixed-address `.LIB` file.

The first version of the interface supports no argument or one `unsigned
int *` in/out argument. It does not describe arbitrary pointer graphs,
strings, or multiple C arguments; those need additional marshaling rules.
