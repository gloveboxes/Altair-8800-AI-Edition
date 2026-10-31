# BANKSDK: shared banked-memory and library support

This folder contains reusable DCC C support for the CP/M banked BIOS ABI 1.0,
call revision 1. Build applications and libraries against it and deploy matching
firmware, boot package and drive A system records together. It is not a runnable example or part of DCC's
platform-independent standard runtime.

| Component | Responsibility |
| --- | --- |
| [BANKLIB.C](BANKLIB.C), [BANKLIB.H](BANKLIB.H) | Load/unload typed libraries, enumerate exports, validate signatures and transfer bounded call frames. |
| [BANKMEM.C](BANKMEM.C), [BANKMEM.H](BANKMEM.H) | Allocate/free banked memory and perform checked byte-buffer reads/writes. |
| [BIOSBANK.C](BIOSBANK.C), [BIOSBANK.H](BIOSBANK.H) | Adapt DCC's C calling convention to the public banked BIOS register ABI. |

These are the canonical implementations. Runnable examples reference them
through their build configurations instead of keeping private copies.
The Altair application's bulk builder excludes `BANKSDK` and its descendants
from application discovery.

## Readable C API

Use the descriptive names below. The headers alias them to six-character
linker symbols with preprocessor macros, which adds no wrapper calls, code or
runtime cost, and function pointers work normally. DCC accepts long C
identifiers, but M80/L80 retain only six significant external-symbol characters
(including DCC's leading underscore); directly exporting several
`bank_library_*` symbols would otherwise collide.

| Library API | Linker symbol |
| --- | --- |
| `bank_library_open` | `blopen` |
| `bank_library_close` | `blclos` |
| `bank_library_check` | `blchk` |
| `bank_library_export_count` | `blcnt` |
| `bank_library_export_name` | `blname` |
| `bank_library_export_signature` | `blsig` |
| `bank_library_call` | `blcall` |
| `bank_library_call_bound` | `blbnd` |
| `bank_library_last_error` | `blerr` |
| `bank_library_set_error` | `blfail` |

| Memory API | Linker symbol |
| --- | --- |
| `bank_memory_initialize` | `binit` |
| `bank_memory_allocate` | `bmalloc` |
| `bank_memory_free` | `bfree` |
| `bank_memory_size` | `bsize` |
| `bank_memory_available` | `bavail` |
| `bank_memory_last_error` | `bmeror` |
| `bank_memory_read` | `bread` |
| `bank_memory_write` | `bwrite` |
| `bank_memory_reset` | `breset` |
| `bank_memory_call` | `bmcall` |

`bank_library_check()` returns nonzero while the library is open and its pool
epoch is current; otherwise it returns 0 with `BL_ARGUMENT` (not open) or
`BL_STALE`. A client that holds its own `bank_memory_*` lease from the same pool
calls it before *writing* to the lease, since the epoch proves the address still
belongs to this owner. Reads need no separate check when a library call that
validates the epoch follows them.

`bank_memory_handle_t` names the bank-offset handle type. Raw BIOS operations use the `bank_bios_*` names declared in
[BIOSBANK.H](BIOSBANK.H). Prefer the checked `bank_memory_*` operations for
allocation-backed buffers. Raw BIOS calls require a successful
`bank_bios_version()` capability check first.

## Contracts and errors

- Library open/close, export-name/signature queries and raw calls return 1
  on success, zero on failure. Check
  `bank_library_last_error()` for the `BL_*` reason and check the exported
  function's result separately. A failed load attempt invalidates the newly
  loaded module; if that cleanup fails, `BL_CLEANUP` takes precedence.
  Trying to open another library while one is already open instead returns
  `BL_ARGUMENT` and preserves the current module.
- Export-name buffers need at least nine bytes. Signature buffers have an
  explicit capacity, at most 256 bytes. Raw call bodies are at most 256 bytes;
  prefer the generated typed wrappers over constructing frames manually.
- Memory initialization validates INFO version 1.0 and a versioned capability
  query. Applications do not inspect private resident signatures or tables.
  `bank_bios_query()` discovers pool geometry, byte-copy entries and the
  structured library service. `bank_bios_library_control()` submits its
  versioned 32-byte request without exposing loader-private mailboxes.
- Memory operations return zero on failure; allocation returns a nonzero
  handle, while size/available report bytes. `bank_memory_free(0)` succeeds.
  `bank_memory_reset()` is destructive to every allocation in the shared pool.
  `bank_memory_last_error()` reports a `BANK_BIOS_*` status, including argument,
  invalid-handle, pinned-release and out-of-memory errors. Raw BIOS wrappers
  expose `bank_bios_last_error()` independently of a function's ordinary result;
  a successful function returning zero is not a BIOS error.
- A failed `onload` hook must release resources it acquired before returning.
  The host invalidates the module but does not invoke `onunload` after a
  partially completed `onload`.

## Call ownership and bounded work

Byte-granular BIOS transfers validate the entire range before copying.
Counts divisible by 16 jump directly to the raw multi-burst path, without
constructing a tail-buffer stack frame. Other counts use stack-local storage to preserve
surrounding bytes, including requests ending at `FF00h`. No C scratch-tail
implementation or shared static transfer buffer is needed.

Checked memory reads/writes share one assembly adapter for DCC's four-argument
stack ABI, avoiding nested C transfer frames. They still call
`bank_memory_size()` for every operation: no cached allocation bounds can survive
free or reset. Offset and length are checked without unsigned wrap before either
memory range is touched. The raw gateway keeps its low count and direction in A,
updates its group count once per 256 bytes, and saves the advancing source SP in
the source bank instead of recomputing its address every burst. It still selects
banks exactly twice per 16 bytes, preserves IX/IY/SP/IFF and occupies 237 bytes.
The payload instructions preserve the main carry flag, so the loop reuses it
for destination-bank dispatch rather than testing direction again.
See [MEMPERF](../MEMPERF/README.md#reproducible-cpu-cycle-measurements) for measured
CPU-cycle savings; hardware throughput requires running the rebuilt benchmark.

Opening reserves one 512-byte pool extent (a 272-byte request rounded to the
allocation unit). It stages the FCB, then reuses that extent for every call.
Steady-state calls perform **no allocation or free**; close and failed open
release it. Failed calls retain it for retry or close and do not publish outputs.
Frame setup clears alignment padding, not the copied body.

Generated typed wrappers use a four-byte `bank_library_binding_t` per function.
They compare the complete canonical signature once per module generation, then
reuse its export index. Native counters verify twenty consecutive bound calls
perform zero additional signature reads, allocations or frees; reload forces
rebinding. Raw `bank_library_call()` still compares the full signature each time.
Do not reuse one binding for different name/signature pairs or mutate loaded
metadata through raw writes. No hardware latency improvement is claimed.

Before writing a reused frame, the runtime queries module generation and pool
epoch. Resident dispatch checks both again. Invalidation/reload makes old bindings
stale; pool RESET makes old frame ownership stale. The runtime never writes or
frees an old frame after its epoch changes, because another owner may now hold
the same address. `BL_STALE` reports this explicitly; close and reopen to recover.

Host and library DMA/search continuations are separate. Search Next reloads the
shared directory sector before resuming, so interleaved searches across drives
work correctly. This adds a directory read to resumed searches. Current drive,
current user and allocation state remain shared, just as ordinary CP/M calls do.
Private context snapshots pack the DMA word and only the seven actual search
bytes; shared disk-geometry scratch is neither saved nor restored. The host BDOS
proxy uses the same search packing as library entry/return. Resident metadata
selectors use flag-preserving bit operations instead of saving AF on the stack,
and SIZE validates live table entries directly without caching their values.
Module generations, pool epochs and per-call object tokens never wrap.

## Use from a client

Include header names normally:

```c
#include "BANKLIB.H"
```

For a client in a sibling example folder, add the shared sources and header
directory to its configuration:

```ini
dcc-input=APP.C, ../BANKSDK/BANKLIB.C, ../BANKSDK/BIOSBANK.C
dcc-include-directory=${DCC_DIR}, ../BANKSDK
```

A memory-only client instead compiles `BANKMEM.C` and `BIOSBANK.C` and includes
`BANKMEM.H`. A client using both APIs compiles all three implementation files.
The [LIBDEMO typed-library example](../LIBDEMO/README.md) shows generated client
wrappers and a composite library/host build; its nested configuration uses
`../../BANKSDK`.

The bank-1 library itself can include `BANKLIB.H` for the runtime handle token
without compiling the host-side transport into the library.
SDK calls are synchronous, and bank-1 code must not re-enter the bank-switch
gateway.

Bank-memory handles are bank offsets, never local C pointers. Objects exposed
by typed libraries use their own opaque handles; the example uses non-repeating
runtime tokens. Do not mix the two kinds of handle.
