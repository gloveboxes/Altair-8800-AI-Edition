# LS

Compact C11 directory listing for CP/M 2.2. It scans raw directory entries so
all extents are included, sorts 8.3 names, and reports allocation-block sizes
without linking `printf` formatting. CP/M filespec filtering and temporary
drive selection are supported. The column heading is bright blue on VT100-compatible
terminals and resets to the terminal's default color before file output.

```text
LS *.COM
LS A:
LS A:*.COM
LS -S
LS -S *.COM
LS -S A:*.COM
LS *.COM -S
LS A:*.COM -S
```

`-S` sorts largest files first and may precede or follow the optional filespec.
Files with equal sizes remain alphabetical. The original drive is restored
before returning to the CCP.

## Build

```sh
export DCC_DIR=$HOME/GitHub/dcc
export NTVCM_DIR=$HOME/GitHub/ntvcm
./build-app.sh
```

The optimized `LS.COM` is 4,096 bytes and occupies 4K on the bundled 2K-block
disk format.

## Install

Transfer COM files in binary mode:

```text
FT -GB FILE://LS/LS.COM
```
