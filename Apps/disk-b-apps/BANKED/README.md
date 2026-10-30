# BANKED — dcc C heap in the extra SRAM bank

This example needs the Z80ROMlessSBC banked firmware and A16 wiring. It is
specific to the SBC; the ESP32 Altair port-7 implementation is not equivalent.
The project copy lives in Z80ROMlessSBC/examples/BANKED; the requested app copy
lives in esp32-altair-8800/Apps/disk-c-apps/BANKED.

Shared bank-memory and BIOS support lives in [BANKSDK](../BANKSDK/README.md);
this example contains only its application-specific code.

Build with the local dcc toolchain:

```sh
export DCC_DIR=/Users/dave/GitHub/dcc
"$DCC_DIR/dccmake"
```

Output: `build/BANKED.COM`. Copy it to a CP/M disk and run `BANKED` after
loading the matching firmware. The app deliberately resets the shared banked
heap, checks allocator fragmentation/reuse and the FEFFh boundary, then
repeatedly writes and verifies all 5,632 usable bytes with 24 patterns. The
patterns include fixed byte values, address-derived values, and walking one
and zero bits. Each pattern writes the entire bank before a separate
read/compare sweep, exposing aliased addresses. Before every verification
read the base buffer is filled with inverse expected values, so a skipped or
partial read cannot pass using stale data. The only base-RAM data buffer is
256 bytes. Startup lists the allocator, boundary and pattern tests.

The normal build starts stopwatch 0 through timer I/O port `0x17`, displays
elapsed and remaining seconds, completed full-bank iterations, and cumulative
successful bank read/write API calls and average data operations per second
every 10 seconds, then stops after 43,200 seconds. The rate counts successful
`bread`/`bwrite` calls since the stopwatch started, rounded down to whole
operations per second; setup calls are excluded. The access counters count
`bread`/`bwrite` data calls, not allocator
metadata traffic. This works with CP/M 2.2 and needs no calendar clock. Press
ESC to stop early. A failed timer read, memory transfer, allocation, or data
comparison prints `FAIL` and exits.

Timer startup first recovers the port parser with IN followed by OUT zero,
so a previous application's incomplete command cannot affect the countdown.

If A16 is not wired, it reports banked SRAM unavailable without switching the
CPU out of its base memory.

```c
#include "BANKMEM.H"

bhandle record;
unsigned char chunk[256];

if (!binit()) { /* firmware or wiring unavailable */ }
record = bmalloc(4096);
if (record) {
    /* Fill chunk in ordinary C, then copy it into the allocation. */
    bwrite(record, 0, chunk, sizeof(chunk));
    bread(record, 0, chunk, sizeof(chunk));
    bfree(record);
}
```

Check each function's result in real applications. `bmalloc(0)` returns zero;
`bfree(0)` succeeds. `bread`, `bwrite`, `bfree`, `binit` and `breset` return 1
for success, 0 for failure. `bsize` reports the rounded allocation capacity,
and `bavail` reports total free bytes (fragmentation can prevent a request
smaller than that total). Transfers may reach any byte within the rounded
capacity and reject overflow. Allocation contents are not cleared. Freed
handles become invalid and may later be reused, as with ordinary malloc.

Allocations use 256-byte units and an address-ordered free-extent list.
Bank 1 reserves `0100h–C7FFh` for the fixed library, DCC heap and stack,
`DE00h–E5FFh` for its loader, and `E600h–EDFFh` for the allocator. The general
heap is `C800h–DDFFh`: **22 blocks, 5,632 bytes**. Allocation splits free extents;
free merges adjacent ones. The size of a live allocation is read directly
from a table. Available bytes are maintained in a counter. There is no
per-allocation metadata inside payloads or bank-0 map cache.

All heap operations run in bank 1 through BIOS HEAP (`FF10h`). Calls mask
interrupts, use a private bank-1 stack, and restore the caller's bank, SP,
IX/IY and interrupt state. No BDOS or base-bank C stack access runs there.
Payload copies still use 16-byte register bursts through READ/WRITE. Partial
transfers use a 16-byte bank-0 buffer. CP/M's DMA area is untouched.

`binit` checks BIOS ABI 2 and the installed `BKM3BEX1` header. `breset`
invalidates all allocations without changing code. Warm boot preserves the
heap; firmware reload initializes it. One application owns the shared heap.

Install matching **ABI 2.6 firmware, boot package, drive A system records and
rebuilt COM files** together. INFO reports `0206h`, heap start `C800h`, and
capacity `1600h`. See [BIOS-ABI.md](BIOS-ABI.md)
for HEAP operations, contracts and the extent table layout.

The same C library and COM pass the local native-copy BIOS backend model
without touching the SBC's workspace or port 7. The stock ESP firmware still
needs its own compatible BIOS extension and extra memory; copying this C app
does not install that backend. The stock interactive debugger
has flat memory; use the dedicated banked emulator fixture for this app:

```sh
DCC_DIR=/Users/dave/GitHub/dcc /Users/dave/GitHub/dcc/dccmake \
  dcc-define=BANKED_ONE_CYCLE dcc-build-dir=build/emulator
```

Build this finite test version from `examples/BANKED`, then run from the
Z80ROMlessSBC root:

```sh
build/disk-emulator/bin/python src/common/tests/banked_heap_z80_test.py \
  --program examples/BANKED/build/emulator/BANKED.COM
```

Run that command from Z80ROMlessSBC. This executes the compiled COM under the
real CP/M/BIOS image with modeled A16. It checks software behavior; 20 MHz
electrical timing and throughput require the physical board.

See [TESTING.md](TESTING.md) for verification results and the corrected host
test fixtures. The full host-control suite passes all 78 tests.
