# DATE — print the host date and time, DATE-style

`DATE.COM` prints the host machine's current local date and time in the same
field layout as the CP/M `DATE` command, e.g.:

```
A>DATE
Sun 05/31/2026 17:13:00
```

## Why it exists

The Altair has no real-time clock, so:

- Under CP/M 2.2 there is no `DATE` command at all.
- Under CP/M 3 the built-in `DATE` shows a frozen value, and its two-digit year
  formatter overflows for years from 2000 on (31 May 2026 prints as
  `Sun 05/31/<6 17:13:00`, because it renders `year - 1900 = 126` without a
  `mod 100`).

`DATE.COM` sidesteps both problems by reading the real wall-clock time from the
host and printing a correct **four-digit** year. It works on **both CP/M 2.2 and
CP/M 3** because it uses only console output (BDOS function 2), not the CP/M 3
clock BDOS calls. Because CP/M runs a transient `.COM` in preference to a
built-in command, this `DATE.COM` also shadows the buggy CP/M 3 built-in `DATE`.

## How it works

The dcc client links `../COMMON/DXSVC.C` and requests clock group `0x20`,
operation `0x04`, through the framed ROMLESS-SVC/1 interface on ports 2/3/4.
The response is formatted as `%a %m/%d/%Y %H:%M:%S`; the SDK supplies the
local trailing NUL. Unsynchronized clocks and unavailable services produce
an explicit error and nonzero exit status, not a fabricated date.

Text is printed with `putchar`, without linking the formatted-output runtime.
The current C build is 1,664 bytes and occupies one 2 KB allocation block,
down from 2,304 bytes before shared framing/diagnostic compaction.
The alternate assembly build is 384 bytes:
its full 2,049-byte response workspace is reserved in RAM using `EQU`, not
emitted as executable-file padding. Both retain counted responses, bounded
polling and explicit transport-error recovery.

## Build

Build the C11 version with dcc:

```powershell
$env:DCC_DIR = "$HOME/GitHub/dcc"
$env:NTVCM_DIR = "$HOME/GitHub/ntvcm"
pwsh ./build-app.ps1
```

After a successful build, the script installs `DATE.COM` into the default C:
disk image.

To build without modifying a disk image, use `dccmake` directly; the result
is written to `build/DATE.COM`.

Alternatively, build the assembly version under CP/M:

```
SUBMIT DATE
```

which fetches the source, assembles it (`ASM`), converts the HEX image
(`LOAD`), and produces `DATE.COM`.
