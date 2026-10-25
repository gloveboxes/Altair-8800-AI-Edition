# CLOCK (dcc C11)

`CLOCK.C` links `../COMMON/DXSVC.C` for ROMLESS-SVC/1 transactions on ports
2/3/4. It retains millisecond timer 2 and uses clock group `0x20` for local
time, long local date and decimal uptime, plus weather group `0x40` for
status and selected fields. An unsynchronized clock is an explicit service
error rather than a valid calendar reading.

Unavailable weather and failed weather requests show a centered
`Weather unavailable` message. Display service reads suppress the shared
client's console diagnostics so service errors cannot disrupt the VT100 layout;
failed time reads continue to show the waiting-for-SNTP state. That message
clears only the interior columns, preserving the right-hand screen border.

With `DCC_DIR` set and dcc/ntvcm on PATH, run `dccmake` to create
`build/CLOCK.COM` without changing a disk image. The existing PowerShell
wrapper also installs its result into the configured disk image.

On the Pico/Z80 Stage 10 system, error 21 means the clock is not yet
synchronized. This display fix is in `CLOCK.COM`, not the Pico firmware:
update Drive C with the rebuilt application. Flashing firmware alone does
not update the CP/M executable.

Native weather-display regression checks (from the repository root):

```sh
cc -std=c99 -Wall -Wextra -x c Apps/CLOCK/tests/weather_test.c -o /tmp/clock-weather-test
/tmp/clock-weather-test
```

The shared client's `Apps/COMMON/tests/z80_client_regression.mjs` also
builds CLOCK and executes its compiled Z80 clock reads with error 21,
checking diagnostic suppression and restoration as well as successful reads.
