# Shared CP/M application support

`DXSVC.C` and `DXSVC.H` implement the
[ROMLESS-SVC/1 service protocol](../../docs/docs/en/60-programming/01-ROMless-IO-Mapping.md)
for the current dcc applications. The legacy BDS C apps, SDK and compiler
archives have been removed. This directory is not a replacement BDS SDK.

For new games, read
[Ctrl-S gameplay lessons and game-local raw console buffering](RAW-CONSOLE.md).
Raw keyboard polling must be paired with raw output: buffered stdout can still
invoke BDOS flow control and consume movement keys during drawing.

Ten dcc application configurations compile this shared client: ATTN, BREAKOUT,
CHAT, CLOCK, DATE, HANGMAN, MANC11, SHEETS, SNAKE and TETRIS. Their
`dccmake.txt` files include `../COMMON/DXSVC.C` and the shared include directory.
The client combines typed C service wrappers with embedded Z80 framing,
transport, diagnostics and validated timer entry points. It has no external
data or allocation.
Only the private per-byte write helper is `static inline`; transaction and
convenience helpers remain out-of-line.
Request and reply loops walk pointers with countdowns. Reply state and capacity
checks are outside the byte-copy loop; excess and error bytes are still drained.
Diagnostics use `putchar` and decimal digit subtraction, not `printf` or division,
so this client does not introduce a formatted-output runtime dependency.

The header uses `#pragma once`. Constants explicitly distinguish physical ports
(`SERVICE_COMMAND_PORT`, `TIMER_IO_PORT`, `FILE_COMMAND_PORT`) from shared
command groups (`SERVICE_GROUP_CLOCK`, `SERVICE_GROUP_WEATHER`, and so on).
Public functions share the `svc_` prefix, while implementation parameters,
local variables and assembly labels also use descriptive names. The installed
M80/L80 pipeline links globals by their first six characters, including the
leading underscore. Public names therefore have distinct first letters after
`svc_`: for example, `svc_operation_send` and `svc_query_text` do not collide
with `svc_start_timer`. Preserve this distinction when adding exported helpers.
Internal assembly bridges use `io_` names that also avoid C runtime symbols.
The short API names have been replaced, not retained as aliases; maintained
callers and tests use the new API.
Argument types, return values and wire protocol are unchanged. Framing and
diagnostics share compact assembly paths rather than duplicating generic C
checks at each convenience call.

## API

| Helper | Result |
| --- | --- |
| `svc_begin_request(service_group, request_length)` | Begin a low-level framed request; send payload bytes, then finish the response |
| `svc_finish_response(response, response_capacity)` | Wait for and consume a counted response, or acknowledge completion |
| `svc_call_service(service_group, request, request_length, response, response_capacity)` | Raw request; received byte count, or negative service error |
| `svc_operation_send(service_group, operation, request, request_length, response, response_capacity)` | Streams operation followed by data without a temporary request buffer |
| `svc_get_text(service_group, operation, response, response_capacity)` | Locally NUL-terminated text; returned length excludes NUL |
| `svc_query_text(service_group, operation, selector, response, response_capacity)` | Text query with a one-byte selector |
| `svc_start_timer(timer_id, duration)` | Port 23 countdown: IDs 0-2 milliseconds, ID 3 whole seconds |
| `svc_timer_active(timer_id)` | Port 23: 0 expired or 1 running |
| `svc_random_u16()` | Full unsigned 16-bit value, including zero |
| `svc_access_stopwatch(stopwatch_id, operation, response)` | Port 23: reset (0), seconds (1), milliseconds (2); four little-endian response bytes |

### Dedicated timers

The three timer/stopwatch functions use dedicated port 23 instead of framed
service transactions. All argument types, IDs/units/ranges and return values
are preserved. Their immediate-port assembly needs:

| Operation | Port accesses | Previous shared-service minimum |
| --- | ---: | ---: |
| Start countdown 0-2 | 3 OUTs | 13 |
| Start seconds countdown 3 | 2 OUTs | 13 |
| Query countdown | 1 OUT + 1 IN | 11 |
| Start/reset stopwatch | 1 OUT | 9 |
| Read stopwatch | 1 OUT + 4 INs | 14 |

`svc_start_timer`, `svc_timer_active` and `svc_access_stopwatch` are assembly entry points, including complete
16-bit ID/operation validation and seconds-duration/null-reply checks before
any port access. There is no additional C wrapper or private timer-call layer.
They address arguments through SP without changing IX/IY, and stopwatch reads
are four unrolled IN/store pairs. Errors still call the C diagnostic/exit helper;
an invalid query reply resets only the timer interface before reporting it.

Compared with the previous C wrappers calling private assembly primitives,
using the same dcc peephole settings and real Z80 fixture:

| Operation | Previous cycles | Current cycles | Reduction |
| --- | ---: | ---: | ---: |
| Start millisecond countdown | 366 | 185 | 49.5% |
| Start seconds countdown | 396 | 182 | 54.0% |
| Query expired countdown | 331 | 123 | 62.8% |
| Query running countdown | 367 | 123 | 66.5% |
| Reset stopwatch | 547 | 162 | 70.4% |
| Read stopwatch seconds or milliseconds | 700 | 307 | 56.1% |

Cycle counts include the API entry through RET, but exclude the caller's
argument setup/CALL and host port-callback execution. The linked sizes are
the complete fixture executables, not isolated library code sizes.

These are access counts, not wall-clock speed ratios. Dedicated timers are
synchronous, nonblocking and available while a shared request is BUSY; they
never touch its frame, poll its worker or reset its transport on argument errors.
Framed groups `SERVICE_GROUP_TIMERS`/`SERVICE_GROUP_STOPWATCHES` remain compatible, using the same CPU-owned state.
The full [port-23 ABI](../../docs/docs/en/60-programming/01-ROMless-IO-Mapping.md#dedicated-timer-port-0x17-decimal-23)
also supports persistent countdown selection and a four-bit running mask:
raw clients can select once, then poll with a single IN.

Do not interleave timer port sequences with interrupt-handler or other raw
port-23 users. A stopwatch read latches four bytes at the command. Complete
duration writes before issuing another command; to recover from an unknown
parser state, read port 23 once then write command 0. This resets only the
interface, not deadlines/stopwatches. On valid binary reads, every byte value
(including `0xFF`) is ordinary data.

The transport acknowledges COMPLETE, consumes counted binary/text responses,
drains oversized replies, reports errors and resets after failure. Generic
calls return negative error codes; callers must check them. Timer/random/
stopwatch convenience helpers terminate the app after an error instead of
returning a plausible successful value.

Polling uses 16-bit countdowns bounded to 100 batches of 65,535 status reads. This is an
uncalibrated budget, not a wall-clock deadline: CP/M cannot obtain an independent
clock while the sole shared transaction is busy. RESET abandons transport state
but does not cancel service side effects.

## Embedded dcc assembly core

Assembly is the only production transport implementation. The core is embedded
directly in [DXSVC.C](DXSVC.C) as two dcc `#asm` blocks. Existing app build
configurations need no extra source, header or feature flag:

```sh
dccmake
```

Request framing, response validation/copying/draining, bounded polling,
decimal diagnostics and the three public timer APIs are assembly. Generic
request/text/random wrappers and the timer termination bridge remain C.
Diagnostics use the same dcc `putchar` runtime entry, preserving console
buffering and the exact message for every wire error byte (0-255).
Internal `io_print_error` and `io_fail_service` entries are not public header
APIs. Native builds retain the C framing/diagnostic implementation as a
reference; the Z80 comparison enables it only in a scratch test copy.
The core uses immediate-port `IN`/`OUT`, not ED-prefixed block I/O or
self-modifying code. It follows the dcc caller-cleaned stack ABI and preserves
IX, IY and interrupt-enable state. Zero-length buffer operations perform no I/O.

Each assembly entry point has explicit dcc reachability markers, with a
separate block for shared timer errors. The normal `dccmake` unused-code pass
therefore removes uncalled transport, countdown and stopwatch routines, along
with their C-only dependencies. Text-only clients do not retain timer code;
timer-only clients do not retain framed transport. No app-specific feature
flags or separate sources are required. With `dcc-strip-unused=false`, all
routines remain available as before. Keep these boundaries and cross-block
error-label references intact when changing the assembly.

Measured with dcc peephole optimisation and the project's Z80 emulator:

| Workload | C reference | Assembly core | Reduction |
| --- | ---: | ---: | ---: |
| 60,000 BUSY polls, complete transaction | 14,343,278 cycles | 2,221,109 cycles | 84.5% |
| 2,048-byte request and 2,048-byte reply | 1,238,666 cycles | 169,721 cycles | 86.3% |
| Fixture linking all public helpers, including dedicated timers | 3,712 bytes | 2,560 bytes | 1,152 bytes |

Compared with the preceding assembly client, compact framing/diagnostics
reduce the all-helper fixture from 3,072 to 2,560 bytes and DATE from 2,304 to
1,664 bytes. Countdown/stopwatch cycle counts are unchanged. DATE still uses
the ordinary shared text API; there is no size-saving app-specific shortcut.
The C DATE regression ceiling is 2,048 bytes (one bundled disk allocation block).

Port callbacks supply deterministic replies and consume no simulated CPU
cycles; these are client costs, not service/network latency. The assembly core
preserves the exact polling-read budget, but executes it sooner in real time.
Slow asynchronous/network services must be checked at the intended
CPU/emulator speed. A polling budget is not a portable timeout.

## Checks

Local, Docker and debugger-adapter CTest builds include `cpm-service-client`.
Native builds link [tests/SVCCORE.C](tests/SVCCORE.C), a test-only C reference
that exercises the C API against mocked ports; it is not an app build option.
It checks framing, binary zeros, ACK/RESET, exact 2,048-byte limits, error codes,
oversize draining, stopwatch byte order, decimal diagnostics for all error bytes,
and polling batch/final-read/timeout boundaries.
The `io-timer-port` test additionally links the real client and CPU-side router
against a deterministic monotonic clock, checking all timer IDs, exact expiry
boundaries, seconds rounding, atomic partial writes, recovery, stopwatch
latching/wrap, shared-state compatibility and exact port-access counts.

```sh
sh Apps/ATTN/test-services.sh
sh Apps/CHAT/test-services.sh
```

To rebuild and compare the C and assembly clients on the real Z80 core:

```sh
node Apps/COMMON/tests/z80_client_regression.mjs \
  /absolute/scratch/z80-client /path/to/dcc
```

This requires the dcc tools (including native M80/L80), Node and a native C/C++
compiler. It creates a scratch-only client copy without the assembly blocks and
links the test reference for the C comparison. Production source is not changed.
It runs both implementations against the same port fixtures, checks
binary/empty/max-size replies, full error/excess draining, validation, recovery,
convenience APIs, exact polling boundaries, SP/IX/IY, caller arguments and
enabled/disabled interrupt state. Direct framing tests cover signed/high-byte
length and capacity limits, handshake failures, copy/drain boundaries and all
256 diagnostic error bytes. Timer error tests cover negative/high-byte
IDs/operations, duration overflow, null replies, diagnostics/exit(1), and every
invalid query reply byte (2-255), without resetting the shared channel.
The runner reports linked sizes and simulated cycles. An optional third argument
supplies a saved client C file with matching API names to build/run against the
same fixtures; it asserts that timer/service cycle counts and the linked fixture
size do not regress. Builds stay outside the checkout;
no disk images or deployment files are changed.

The runner also builds the C DATE application, enforces a 2,048-byte size
ceiling, and executes its exact console output, asynchronous response,
error-draining and oversized-reply/nonzero-exit cases on the Z80 core.
Single-helper builds verify text/random, each timer API and the shared error
bridge retain exactly the needed assembly entry points; an unstripped build
checks the opt-out behavior.

For the assembly/BASIC regression suite:

```sh
node Apps/COMMON/tests/asm_basic_regression.mjs \
  /absolute/scratch/build-dir /path/to/mac-load-tools \
  /path/to/ntvcm /path/to/dcc
```

The scratch directory must be outside the checkout. The suite uses MAC/LOAD,
M80/L80, a native C/C++ compiler, cpmtools, Node and the built MCP server.
It normalizes CP/M source line endings, checks service/error/register fixtures
with both MAC/LOAD and production M80/L80 assembly,
and runs finite MBASIC tests against the real MCP backend. Tests use private
disk copies and do not install apps or alter deployment images. Existing MAC
and LOAD tools remain on the supplied CP/M tools images.
Runtime workspace is filled with nonzero bytes before testing; checks include
ENV cache/service-buffer bounds, EDIT line-table initialization and heap/stack
layout, and both assembly ATTN variants' full 1,216-weight conversion.
