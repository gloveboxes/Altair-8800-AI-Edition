# SHEETS

A VT100 spreadsheet written in dcc C11 for CP/M 2.2 on the Z80.

## Implementation

- `sheets_ui_c11.c`: display, keyboard handling, file I/O, grid operations,
  structural edits, and copy/paste relocation
- `sheets_calc_c11.c`: cell ownership, formula normalization, native 32-bit
  evaluation, range functions, cycle detection, and rendering
- `SHEETS.C` and `SHEETC.C`: CP/M 8.3-compatible dccmake entry points
- `SHEETS.H`: shared C11 interface and constants

Console output uses a static 4 KiB fully buffered `stdout` stream. The buffer is
flushed before blocking for keyboard input, reducing BDOS console calls during
screen redraws.

## Grid

- 26 columns (`A` through `Z`) by 99 rows
- 7 columns by 26 rows visible, scrolling with the cursor
- Sparse heap-allocated cell text
- Native signed 32-bit `long` formula arithmetic

## Formulas

Cells beginning with `=` support:

- Integer arithmetic with `+`, `-`, `*`, `/`, unary signs, and parentheses
- Relative and absolute references such as `A1`, `$A$1`, `$A1`, and `A$1`
- `SUM`, `AVG`, `MIN`, `MAX`, and `COUNT` rectangular ranges
- `RAND()` and `RAND(n)`, frozen to a value when entered
- Circular-reference and invalid-reference reporting

Examples:

```text
=A1+B2*3
=(A1+A2)/2
=SUM(A1:B5)
=RAND(6)
```

## Build

Set the dcc and ntvcm locations, then run:

```powershell
$env:DCC_DIR = "$HOME/GitHub/dcc"
$env:NTVCM_DIR = "$HOME/GitHub/ntvcm"
pwsh ./build-app.ps1
```

The optimized build produces `SHEETS.COM`, removes intermediate files, and
installs the program into the default C: disk image. From the repository root,
run it with a CP/M filename argument:

```sh
ntvcm Apps/SHEETS/SHEETS.COM TEST
```

## File format

One non-empty cell is stored per line:

```text
A1=42
B1=Hello
C1==SUM(A1:A5)
```
