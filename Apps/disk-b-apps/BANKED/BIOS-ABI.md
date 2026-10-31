# Banked-memory BIOS extension ABI 1.0

Call the mirrored gateway from bank 0. Check `BMX1` at `FF09h`, major 1 at
`FF0Dh`, minor 0 at `FF0Eh`, and a successful INFO call. Firmware and the CP/M
system image must be deployed together.

| Address | Entry | Registers and result |
| --- | --- | --- |
| `FF00h` | READ | HL=bank-0 buffer, DE=bank-1 address, BC=length |
| `FF03h` | WRITE | HL=bank-0 buffer, DE=bank-1 address, BC=length |
| `FF06h` | INFO | HL=`0100h`, DE=`C800h`, BC=`1600h` on success |
| `FF10h` | Service | E=operation, HL=argument/result |

READ/WRITE require lengths divisible by 16 and ranges ending no later than
`FF00h`. Success returns A=0/carry clear. Higher-level `bread`/`bwrite`
handle partial transfers through the checked `F806h`/`F809h` entries, which
validate the live allocation extent on every call. Raw access to pinned resident regions is reserved
for system code.

| E | Service operation |
| --- | --- |
| 0 | ALLOC requested HL bytes; returns aligned handle or zero |
| 1 | FREE handle HL; returns 1 on success |
| 2 | SIZE handle HL; returns rounded capacity or zero |
| 3 | AVAILABLE; returns total free bytes |
| 4 | RESET general pool; invalidates its handles |
| 5 | CALL code handle HL, distinct stack handle BC, entry offset IX, context IY |
| 6 | Bank-0 BDOS disk/file proxy (used by the CP/M shim) |
| 7 | Versioned capability query: pool geometry and public service descriptors |
| 8 | Versioned LOAD/INVALIDATE/QUERY/CALL library request |
| 9 | Last service status, independent of a function's HL result |

Use the [shared SDK](../BANKSDK/README.md) to discover services rather than
reading private allocator tables. Byte transfers use BIOS helpers; aligned
portions use the multi-burst path. DMA/search continuations are isolated
between host and library, while current drive/user are shared.

The general allocation pool is `C800h–DDFFh`: 22 extents of 256 bytes,
5,632 bytes total. The extent allocator lives at `E600h–EDFFh`. Its pinned
library handle `0100h` and stack handle `C000h` cannot be freed or reset.
Allocation searches free extents; freeing coalesces physical neighbors.
All operations return to bank 0 with IX, IY, SP and interrupt enable restored.
Calls are sequential, and bank-1 code must not re-enter the gateway.

CALL pushes IY on the bank-1 stack as one 16-bit DCC argument, then the
return address. A normal DCC function reads it at `IX+4` after its prologue
and returns its 16-bit result in HL. The gateway saves bank-0 SP, switches to
its bank-1 service stack, and then switches to the supplied call stack. After
`RET`, it restores bank-0 SP and bank selection. No bank-0 pointer or stack
address is directly usable while bank 1 is selected.

Library LOAD validates a `.LIB` header and loads one fixed-origin image at
`0100h`; INVALIDATE makes it uncallable. Up to eight named, signed exports are
published. The SDK runs `onload` and `onunload`.
See the [BIOS documentation](https://gloveboxes.github.io/Z80ROMlessSBC/cpm-dcc/bios/)
and [LIBDEMO example](../LIBDEMO/README.md) for the full design.

Boot logs separately report bank-1 load and verify progress through
`8960/8960`. On success they report the BDOS, loader and extent allocator as
ready. A failed isolation probe or readback leaves bank 1 disabled.
