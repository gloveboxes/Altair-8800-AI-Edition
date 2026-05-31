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

The Altair emulator exposes the host clock through two I/O ports, implemented in
[`altair_local/PortDrivers/time_io.c`](../../altair_local/PortDrivers/time_io.c)
(`format_now_string`):

| Port | Dir | Purpose |
|------|-----|---------|
| 31   | OUT | Capture host local date/time; stage a printable NUL-terminated string |
| 200  | IN  | Read the staged string back, one byte at a time (0 byte = end) |

The staged string is formatted as `%a %m/%d/%Y %H:%M:%S`. `DATE.COM` does
`OUT 31`, then loops `IN 200` printing each character until it reads a `0`.

## Build

```
SUBMIT DATE
```

which fetches the source, assembles it (`ASM`), converts the HEX image
(`LOAD`), and produces `DATE.COM`.
