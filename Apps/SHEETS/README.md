# SHEETS

A tiny VT100 spreadsheet for BDS C 1.6 running under CP/M on the
Altair 8800 emulator. Patterned after `EDIT` and `CLOCK` for keyboard
and screen handling.

## Grid

- 26 columns (`A`..`Z`) by 99 rows
- 7 columns x 20 rows visible, scrolls with the cursor
- Cells hold text, an integer, or a formula
- 32-bit signed integer arithmetic (-2147483648..2147483647) via the
  BDS C long library (`SDK/LONG.C`)

## Keys

| Key            | Action                                |
|----------------|---------------------------------------|
| Arrow keys     | Move cursor                           |
| Enter          | Edit current cell (keep contents)     |
| Any printable  | Start a fresh edit with that char     |
| Backspace      | (in edit) delete left                 |
| ESC            | (in edit) cancel; (in nav) quit       |
| Ctrl-K         | Clear current cell                    |
| Ctrl-O         | Write file                            |
| Ctrl-L         | Reload file                           |
| Ctrl-G         | Go to cell (e.g. `C12`)               |
| Ctrl-W         | Help                                  |
| Ctrl-Q         | Quit                                  |

## Formulas

Cells whose content starts with `=` are formulas. Supported:

- Integers and unary minus: `=-42`
- Binary `+ - * /` with normal precedence
- Parentheses: `=(A1+A2)/2`
- Cell references: `=A1+B2*3`
- Range functions over a rectangular block:
  - `=SUM(A1:B5)`   sum of all cells in the range
  - `=AVG(A1:B5)`   integer average (truncated)
  - `=MIN(A1:B5)`   smallest value
  - `=MAX(A1:B5)`   largest value
  - `=COUNT(A1:B5)` number of non-empty cells

Cycles and other errors render as `#ERR` in the cell.

## File format

Plain text, one non-empty cell per line:

    A1=42
    B1=Hello
    C1==SUM(A1:A5)

## Build

```
SUBMIT SHEETS
```

`SHEETS.SUB` fetches `SDK/STRING.C` for `strncpy`, then compiles and
links `SHEETS` against it.
