# CLOCK (dcc C11)

`CLOCK.C` links `../COMMON/DXSVC.C` for ROMLESS-SVC/1 transactions on ports
2/3/4. It retains millisecond timer 2 and uses clock group `0x20` for local
time, long local date and decimal uptime, plus weather group `0x40` for
status and selected fields. An unsynchronized clock is an explicit service
error rather than a valid calendar reading.

Unavailable weather and failed weather requests show a centered
`Weather unavailable` message. Weather requests suppress the shared client's
console diagnostics so errors cannot disrupt the VT100 layout; other service
diagnostics retain their default behavior.

With `DCC_DIR` set and dcc/ntvcm on PATH, run `dccmake` to create
`build/CLOCK.COM` without changing a disk image. The existing PowerShell
wrapper also installs its result into the configured disk image.

Native weather-display regression checks (from the repository root):

```sh
cc -std=c99 -Wall -Wextra -x c Apps/CLOCK/tests/weather_test.c -o /tmp/clock-weather-test
/tmp/clock-weather-test
```
