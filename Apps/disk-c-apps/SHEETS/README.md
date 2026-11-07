# SHEETS

A VT100 spreadsheet written in dcc C11 for CP/M 2.2 on the Z80.

The formula engine's `RAND` uses ROMLESS-SVC/1 utility group `0x21`,
operation 0, through `../COMMON/DXSVC.C` on shared ports 2/3/4. The result
retains the existing 0–32767 range. Both 8.3 entry-point sources and the
service helper are linked by `dccmake.txt`.

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
Grid rows keep one text colour across ordinary cells, and cursor movement
repaints only the old and new cells.

## Grid

- 26 columns (`A` through `Z`) by 99 rows
- 7 columns by 26 rows visible, scrolling with the cursor
- Sparse heap-allocated cell text
- Native signed 32-bit `long` formula arithmetic
- A 64-entry formula cache with two slots per bucket and cached display text
- Edits invalidate cached formulas whose transitive dependency bounds include
  the changed cell; structural edits clear the entire cache

## Recalculation performance

Numeric cells retain a parsed 32-bit value after their text, so range scans do
not repeatedly convert decimal strings. Range functions traverse cell pointers
and check the active formula stack once per rectangle. References bypass
function-name matching, small parser and cache helpers use `static inline`, and
decimal formatting uses subtraction instead of software 32-bit division.

Dependency bounds conservatively enclose all referenced cells, including empty
range cells and dependencies of nested formulas. Edits inside those bounds can
cause extra recalculation; edits outside them retain both values and formatted
display text. Errors and `COUNT` ranges containing formulas remain uncached to
preserve the existing circular-reference rules. Cached dependency heights
preserve the 25-formula recursion limit regardless of evaluation order.

The dcc/ntvcm benchmark below compares this pass with the preceding 64-entry
cache implementation. Values are emulated Z80 cycles per iteration, excluding
setup and terminal output. An iteration renders 24 arithmetic formulas, 20
range or fanout formulas, 182 warm display cells, or one collision test formula.

| Workload | Before | After | Fewer cycles |
| --- | ---: | ---: | ---: |
| Arithmetic references | 2,543,340 | 1,572,624 | 38.2% |
| SUM over 99 numeric cells | 38,312,457 | 7,657,249 | 80.0% |
| Shared dependency chain | 1,692,859 | 1,607,730 | 5.0% |
| Warm formula display | 11,738,964 | 459,332 | 96.1% |
| Unrelated edit and redraw | 2,553,196 | 110,124 | 95.7% |
| Repeated cache collisions | 15,441,798 | 860,926 | 94.4% |

This trades memory for speed: approximately 1.1 KiB more static cache/parser
state and four extra heap bytes per numeric cell. The application COM grew from
37,376 to 42,240 bytes in this build.

Run the CP/M calculator checks and cycle benchmarks with dcc/ntvcm on `PATH`
(or set `DCC_DIR` and `NTVCM_DIR`):

```sh
python3 tests/run_calc.py --bench
```

The runner builds in a temporary directory. It subtracts the cycles for one
iteration from eleven iterations and divides by ten to exclude setup costs.
Use `--source /path/to/older/SHEETS --bench-only` to compare an older source tree.

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
