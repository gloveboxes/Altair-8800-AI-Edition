# CLOCK (dcc C11)

`CLOCK.C` links `../COMMON/DXSVC.C` for ROMLESS-SVC/1 transactions on ports
2/3/4. It retains millisecond timer 2 and uses clock group `0x20` for local
time, long local date and decimal uptime, plus weather group `0x40` for
status and selected fields. An unsynchronized clock is an explicit service
error rather than a valid calendar reading.

With `DCC_DIR` set and dcc/ntvcm on PATH, run `dccmake` to create
`build/CLOCK.COM` without changing a disk image. The existing PowerShell
wrapper also installs its result into the configured disk image.
