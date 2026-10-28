# Banked-memory BIOS extension ABI 2.4

Call all entries from bank 0. Check `BMX2` at `FF09h`, major 2 at `FF0Dh`,
minor 4 at `FF0Eh`, and granularity 16 at `FF0Fh` before calling INFO.
Require INFO success before READ, WRITE or HEAP. This greenfield extent
layout requires matching firmware, boot package, drive A system records and
rebuilt COM files; it does not support the earlier two-bit-map layout.

| Address | Entry | Registers/result |
| --- | --- | --- |
| `FF00h` | READ | HL=base buffer, DE=bank offset, BC=byte count |
| `FF03h` | WRITE | HL=base buffer, DE=bank offset, BC=byte count |
| `FF06h` | INFO | Success: A=0, carry clear, HL=`0204h`, DE=`0800h`, BC=`F700h` |
| `FF10h` | HEAP | E=operation, HL=argument/result; see below |

INFO failure returns A=1, carry set, HL=0. Success reports version 2.4,
heap start `0800h`, and capacity 63,232 bytes (247 blocks of 256 bytes).

## READ and WRITE

Positive lengths must be multiples of 16; addresses need not align. The base
buffer starts at or above `0100h`, and both ranges end at or below `FF00h`
without wrapping. Raw bank offsets below `0800h` are accessible for BIOS and
benchmark tooling, but belong to the allocator and are not application data.
Zero length succeeds without touching memory or switching banks.

Success returns A=0/carry clear; invalid arguments return A=2/carry set without
switching banks. IX, IY, SP and interrupt enable are preserved. Main and
alternate AF/BC/DE/HL are scratch. Calls finish in bank 0. The higher-level
`bread`/`bwrite` support arbitrary lengths with a partial-burst buffer.

## HEAP

| E | Operation | HL input | HL result |
| --- | --- | --- | --- |
| 0 | ALLOC | Requested bytes | Aligned handle, or 0 |
| 1 | FREE | Handle | 1 success, 0 invalid handle |
| 2 | SIZE | Handle | Rounded allocation capacity, or 0 |
| 3 | AVAILABLE | Ignored | Total free bytes |
| 4 | RESET | Ignored | 1; invalidates every allocation |
| 5 | CALL | Live code handle in HL; BC=0 or a distinct live stack handle | Value returned by bank-1 code in HL |
| 6 | CALL AT | Live code handle in HL; BC=distinct nonzero stack handle; IX=16-bit entry byte offset; IY=bank-1 context pointer | Value returned by bank-1 function in HL |

All operations return A=0/carry clear; HL reports the outcome. Unknown
operations return HL=0. Null FREE succeeds. SIZE and FREE reject unaligned,
interior, reserved and freed handles. Handles may be reused after freeing.
ALLOC rounds up to 256 bytes, rejects zero/oversized requests, and chooses
the lowest-address free extent that fits. Payload contents are not cleared.
Free space can be fragmented, so AVAILABLE does not guarantee one allocation
of that size will succeed. Allocation ownership belongs to the application.

The resident gateway masks interrupts and selects bank 1. The extent code
saves the caller SP in bank-1 workspace, uses a private bank-1 stack for
assembly helper calls, and restores SP before jumping through the mirrored
gateway back to bank 0. IX, IY, SP and interrupt enable are preserved.
No BDOS, interrupt handler or base-bank C stack access runs in bank 1.
Calls are non-reentrant; interrupt-handler use and NMI during banking are
unsupported. The board holds INT#/NMI# inactive.

CALL validates that HL is the start of a live allocation. BC=0 calls it on
the 64-byte private stack, leaving 58 bytes for the payload. A distinct live
stack handle in BC uses that allocation instead: SP starts at its exclusive
end, and a return address is placed there. The payload must finish with `RET`
and keep its stack use within the allocation. The BIOS restores the private
stack, IX/IY and bank-0 state, then returns the payload's HL value.
The payload receives the caller's IY unchanged. A caller may place a live
bank-1 context handle in IY; CALL does not validate that third handle.

The payload must not re-enter the allocator, call bank-0 code, or enable
interrupts. The caller keeps both handles live until return. Use
`bmexec(code, 0)` for the private stack or `bmexec(code, stack)` for an
allocated stack. Both use E=5. See
`examples/BANKCODE` for a direct-offset prime example.

CALL AT validates the live code and stack handles, rejects the same handle
for both, and requires `offset < rounded code capacity`. Offset zero enters
the start of the code allocation. The BIOS sets SP to the exclusive end of
the allocated stack, pushes IY as one 16-bit argument, and pushes the return
address. A normal DCC C function can read that argument at `IX+4` after its
frame prologue and return a 16-bit result in HL. The BIOS restores the
caller's IX/IY/SP and bank-0 state. The context pointer is not validated;
the caller must provide a live bank-1 object or zero if the function allows
it. The caller/loader is responsible for supplying a genuine instruction
offset inside the code allocation and for relocating absolute references.
Operation 6 requires a separate allocated stack even for entry offset zero.

## Extent layout

| Bank-1 addresses | Purpose |
| --- | --- |
| `0000h–0007h` | `BKM3BEX1`: layout and installed-allocator signatures |
| `0008h` | Firmware allocator revision: 1 supports E=5 on the private stack; 3 accepts an optional stack handle; 4 adds E=6 direct-offset C calls |
| `0010h` | Address-ordered free-list head (block index; 0=end) |
| `0012h–0013h` | Saved bank-0 SP |
| `0014h–0015h` | Cached free-byte count |
| `0016h–0020h` | Assembly scratch |
| `0100h–01FFh` | Extent lengths in blocks, indexed by start block |
| `0200h–02FFh` | Previous physical extent start |
| `0300h–03FFh` | Next free extent; FF marks an allocated extent |
| `0400h–04FFh` | Previous free extent |
| `0500h–07BFh` | Reserved allocator code area |
| `07C0h–07FFh` | Private assembly stack |
| `0800h–FEFFh` | Application heap: 63,232 bytes |
| `FF00h–FFFFh` | Mirrored resident transfer/gateway page |

Only extent starts have nonzero lengths. Interior table slots have length
zero; their other fields are unspecified. Physical predecessor links permit
immediate left-neighbor checks. The next physical extent is start+length.
Adjacent free extents are merged during FREE, so none remain adjacent.
The doubly linked free list is address ordered: allocation visits only free
extents, splits on demand, and unlinks in constant time. FREE unlinks free
neighbors directly, merges once, and inserts the result in address order.
Insertion may walk the free list; FREE is not worst-case constant time.
SIZE and AVAILABLE use direct table/counter reads.

Firmware initializes and verifies the entire reserved region and mirrors the
boot package's exact resident page. Initially the free head is block 8, its
length is 247, and free bytes are F700h. RESET restores this state without
overwriting code or the active stack. Warm boot preserves the banked heap;
firmware reload initializes it. BANKMEM validates the eight-byte signature
before making heap calls, preventing execution of a missing allocator image.

## C interface

`BIOSBANK.C` adapts dcc stack arguments to the register ABI. `bmaloc`,
`bmfree`, `bmsize`, `bmavl` and `bmrset` call HEAP; `bmstat` calls INFO;
`bmread`/`bmwrit` call READ/WRITE. `BANKMEM.C` owns no allocator map or
allocation-size cache in bank 0. It checks initialization and transfer bounds.
The dcc runtime and CP/M DMA buffer are unchanged.

Boot logging reports separate `bank 1 SRAM load` and `bank 1 SRAM verify`
passes from 0/2304 through 2304/2304, after the base-bank load and verification. The count
covers the 2048-byte reserved runtime plus the 256-byte mirrored resident
page; application payload RAM is not initialized. A successful readback prints
`extent allocator and resident page ready`. Probe/readback failures are
reported explicitly and leave bank 1 disabled.

Bank 0 is fully loaded and verified before bank-1 initialization. The A16
probe saves, restores and checks bank 0's reset-vector byte, including when
banks alias. The boot loader checks that byte again before releasing reset.
