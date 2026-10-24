# ROMless Z80 I/O migration reference

**Phases 2 and 3: implemented in the shared drivers and maintained apps.**
The reduced-address ABI is based on the
[ROMless I/O allocation](https://gloveboxes.github.io/Z80ROMlessSBC/system/io-ports/),
reviewed on 20 October 2026. It is not an implementation of services in the
physical ROMless supervisor firmware. Maintained CP/M C, assembly and
BASIC clients use the new interface. Older service-using binaries must still
be rebuilt and installed; source changes do not update CP/M media.

The migration has three phases:

1. Establish this old-to-new reference.
2. Implement the service transport and routing in
   [`io_ports.c`](https://github.com/gloveboxes/Altair-8800-AI-Edition/blob/main/port_drivers/io_ports.c)
   and its drivers, preserving the emulator's terminal/disk interface.
3. Update the shared client and applications under
   [`Apps/`](https://github.com/gloveboxes/Altair-8800-AI-Edition/tree/main/Apps)
   and rebuild affected CP/M programs.

The [legacy port reference](01-i8080-IO-Ports.md#legacy-service-port-reference)
documents the pre-migration interface. There are no legacy service-port aliases.
ESP32, local and Docker compile the same transport and service dispatcher;
only the underlying platform drivers differ.

## Hardware constraints and allocation

Only A0, A1, A2 and A4 are decoded: `decoded_port = address & 0x17`.
There are **16 physical port addresses**, not 32 or 256. Use their canonical
addresses below; ignored bits do not create additional peripherals.

**Interrupt timer port 52 cannot remain:** decimal 52 is `0x34`, and
`0x34 & 0x17 = 0x14`, the fixed disk-data port. Move it to spare port `0x07`.
Its independent one-byte interface is useful even while a shared service is
busy, and avoids a multi-byte transaction to configure/read the interrupt rate.

| Hex | Decimal | Direction | Proposed allocation |
|-----|---------|-----------|---------------------|
| `0x00` | 0 | IN/OUT | **Fixed ROMless terminal data**, unchanged |
| `0x01` | 1 | IN | **Fixed ROMless terminal status**, unchanged |
| `0x02` | 2 | OUT / IN | Shared service command / transaction status |
| `0x03` | 3 | OUT | Shared service request framing and parameters |
| `0x04` | 4 | IN | Shared service response framing and bytes |
| `0x05` | 5 | OUT | Reserved BIOS/qualification diagnostics, unchanged |
| `0x06` | 6 | OUT | Reserved diagnostic data, unchanged |
| `0x07` | 7 | OUT / IN | Interrupt timer rate / configured rate |
| `0x10` | 16 | OUT / IN | **Fixed ROMless disk command / status**, unchanged |
| `0x11` | 17 | OUT | **Fixed ROMless disk drive selection**, unchanged |
| `0x12` | 18 | OUT | **Fixed ROMless disk sector address low**, unchanged |
| `0x13` | 19 | OUT | **Fixed ROMless disk sector address high**, unchanged |
| `0x14` | 20 | IN/OUT | **Fixed ROMless disk data**, unchanged |
| `0x15` | 21 | OUT / IN | Remote file-transfer command / status |
| `0x16` | 22 | OUT / IN | Remote file-transfer filename / chunk bytes |
| `0x17` | 23 | OUT / IN | Dedicated countdown/stopwatch commands, parameters and results |

This uses all 16 addresses, including two reserved diagnostic addresses.
The project's timer extension occupies the ROMless page's spare `0x17`.
File transfer uses the ROMless page's suggested dedicated pair;
network chunks do not occupy the shared response stream.

### Emulator versus physical hardware

The emulator does **not** currently implement the ROMless terminal/disk ABI.
Its CPU adapter intercepts terminal ports `0x00/0x01` and `0x10/0x11`, disk
ports `0x08/0x09/0x0A`, and front-panel input `0xFF` before forwarding other
ports to the service router. Preserve that routing for the existing CP/M disks.

The physical Z80 instead retains its own fixed terminal/disk ports from the
table above. Port this **service ABI**, not the emulator's terminal/disk BIOS,
to the ROMless supervisor. In particular, do not apply `& 0x17` globally in
the emulator: that would alias its existing disk ports onto terminal/service
ports. The new service addresses do not conflict with its current intercepts.

Legacy service addresses are not compatibility aliases on the physical board.
For example, old response port 200 (`0xC8`) decodes to terminal data `0x00`,
and old file command port 60 (`0x3C`) decodes to disk data `0x14`.
Unmodified applications must not run against the new service ABI.

## Shared transport conventions

Command bytes identify **service groups**, not physical ports. Every grouped
request starts with a one-byte operation selector; additional bytes identify
instances or carry parameters.

| Command byte | Group |
|--------------|-------|
| `0x00` | ACK: acknowledge COMPLETE or a fully read error |
| `0x01` | RESET: abandon/reset the shared transport |
| `0x02` | IDENTIFY: return the text `ROMLESS-SVC/1` |
| `0x10` | Countdown timers |
| `0x11` | Stopwatches |
| `0x20` | Clock/date/uptime |
| `0x21` | Utilities/system information/reboot |
| `0x30` | Persistent environment variables and CLI |
| `0x40` | Weather |
| `0x50` | Chat |
| `0x60` | Host-only Sense HAT panel/sensors |

Other command bytes are reserved. All numbers in request/response layouts are
bytes unless explicitly written as `u16` or `u32`.

CP/M clients are described in the [shared client reference](https://github.com/gloveboxes/Altair-8800-AI-Edition/blob/main/Apps/COMMON/README.md).
The dcc transport streams requests, consumes counted responses,
acknowledges completion, reports errors and resets after failure. Its finite polling budget
is not a calibrated elapsed-time deadline: CP/M cannot obtain an independent
clock while the shared channel is occupied.

### Framing and status

The implemented framing deliberately replaces both NUL-terminated transport strings
and unframed binary reads from port 200:

1. Wait for READY on `IN 0x02`.
2. Write the group command to `OUT 0x02`.
3. Write a **u16 payload length**, low byte first, to `OUT 0x03`, followed by
   exactly that many payload bytes. Execution starts only after the last byte.
4. Poll `IN 0x02` with a bounded timeout. A transaction completing is not a
   countdown expiring or a chat stream finishing.
5. For RESPONSE_READY, read a u16 length from `IN 0x04`, then exactly that
   many payload bytes. Consuming the complete frame returns to READY.
6. For COMPLETE (no response), write ACK (`0x00`) to `OUT 0x02`.
7. For ERROR, read the error frame from `0x04`, then ACK or RESET.

ACK, RESET and IDENTIFY are transport commands: they execute immediately,
with no length or request bytes. IDENTIFY is accepted only in READY and returns
a normal length-prefixed response.

| Status from `IN 0x02` | Meaning |
|----------------------|---------|
| `0x00` | READY: no outstanding shared transaction |
| `0x01` | COMPLETE: successful command with no response; ACK required |
| `0x02` | RESPONSE_READY: a complete response frame is available |
| `0x03` | COLLECTING: receiving request length or payload |
| `0x04` | BUSY: execution queued/in progress |
| `0xFF` | ERROR: error frame available; recovery required |

Status reads do not acknowledge completion or advance response bytes. This is
an intentional refinement of the ROMless page's illustrative read-to-ack
example, making repeated status polling safe.

- `u16` and `u32` use **little-endian** byte order on the wire, independent of
  a compiler's in-memory representation. Text excludes its trailing NUL.
  Binary zero is ordinary data, not end-of-response.
- The maximum shared payload is **2048 bytes**, excluding the two
  framing bytes. Validate the length before accepting the payload. Larger
  logical chat requests are assembled using multiple APPEND transactions.
  Never truncate a response and report success.
- Only one shared transaction may be outstanding. Other group commands during
  COLLECTING, BUSY, COMPLETE, ERROR or an unread response are rejected with an
  explicit busy/state error; abandon that transaction's transport result,
  invalidate late completions and require RESET. Do not execute the new command.
  ACK is valid only for COMPLETE or a fully consumed error frame.
- RESET is accepted in any state. It discards the partial request/response and
  invalidates late results, returning READY. It does not undo committed writes,
  reset running timers, close file transfer or cancel a submitted chat job.
  Service-level reset/close operations remain separate.
- Reading `0x04` without an available response, or writing unexpected/excess
  request bytes, reports a state/underflow error. An electrical zero returned
  on an invalid read is not a successful result; the caller must check status.
- ERROR responses use the same u16 frame length. Their payload is one error
  code followed by optional diagnostic text. The status remains ERROR until
  acknowledged/reset, including after the error frame is consumed.
- Define error codes as: `0x01` unknown group, `0x02` unknown operation,
  `0x03` invalid length, `0x04` invalid parameter/range, `0x05` unavailable,
  `0x06` busy, `0x07` timeout, `0x08` invalid state, `0x09` response underflow,
  `0x10` storage open, `0x11` storage read, `0x12` storage write,
  `0x13` capacity exceeded, `0x14` not found, `0x15` clock unsynchronized,
  and `0x16` invalid reboot confirmation.

Network, flash, calendar/utility and other non-timer grouped operations execute
on a dedicated worker (Core 0 on ESP32, a background thread on the host).
Countdown/stopwatch groups `0x10/0x11` are synchronous compatibility interfaces
to the same CPU-thread-owned state as dedicated port `0x17`; they never queue
timer work. The CPU-side timer fast path only reads the monotonic clock and
updates deadlines/latches: it does not block, allocate, or perform I/O.
Network, flash and reboot work stays outside the CPU handler.
After RESET, the previous worker job may still be finishing; a new
worker-backed transaction reports BUSY until that result has been discarded.
Client polling needs an elapsed-time
deadline and RESET recovery, not an uncalibrated infinite loop. Timer interrupts
and dedicated file transfer/countdown/stopwatch ports remain independent of
shared transactions.

Completion polling uses an acquire/release atomic flag. On ESP32 the worker
publishes that flag only after enqueueing its response, so empty BUSY polls
avoid the FreeRTOS response queue. The queue remains the result handoff.
Status/command accesses and direct response reads while BUSY check completion;
normal payload bytes and independent file/interrupt/timer ports do not. Before
submitting a complete request, the transport also collects any abandoned
completion that arrived during request collection. Generation checks prevent
it from replacing a newer frame after RESET.

The native `io-ports-hot-path` test checks these polling boundaries and stale
completion recovery. To benchmark transport byte handling with a deterministic
echo backend (not network/worker latency):

```sh
cmake --build altair_local/build --target io-ports-hot-path-test
altair_local/build/io-service-tests/io-ports-hot-path-test --benchmark
```

It reports median request/reply times and worker-poll counts for 1-, 64- and
2,048-byte payloads. A normal transaction that reads status once now polls
the worker three times, independent of payload length, rather than on every
port access. Protocol checks and buffer copies remain unchanged.

## Dedicated timer port: `0x17` (decimal 23)

All countdown and stopwatch operations are available without shared-service
framing, BUSY polling or ACK. Command/parameter writes and result reads all use
port 23. Commands encode both the operation and the instance:

| OUT command | Parameters following on OUT 23 | IN 23 result |
|-------------|--------------------------------|--------------|
| `0x00` | None | Reset **interface only**, then countdown running mask |
| `0x10`-`0x12` | Duration u16 little-endian | Arm millisecond countdown 0-2; then running mask |
| `0x13` | Duration u8 | Arm seconds countdown 3; then running mask |
| `0x20`-`0x23` | None | Select countdown 0-3; repeated reads return 0 expired/unset, 1 running |
| `0x30` | None | Repeated reads return bits 0-3: countdown 0-3 running flags |
| `0x40`-`0x42` | None | Start/reset stopwatch 0-2; then countdown running mask |
| `0x50`-`0x52` | None | Latch stopwatch 0-2 seconds; read exactly four u32 little-endian bytes |
| `0x60`-`0x62` | None | Latch stopwatch 0-2 milliseconds; read exactly four u32 little-endian bytes |

Countdown durations/ranges and stopwatch wrap/initial behaviour match the
compatibility groups below. A countdown is replaced only when its last
duration byte is written; partial writes do not change a running timer.
All four stopwatch bytes come from the command's timestamp, even if time
advances between reads. A new command may discard an unread latch.
Initial mode is the countdown running mask.

Invalid commands, interrupted parameter writes and reads past a four-byte
stopwatch latch enter an error state. IN returns `0xFF` until the interface is
reset; non-reset OUTs in that state are ignored. To recover from **any**
parser state, issue **one IN 23, then OUT 23,0**. IN during parameter collection
aborts the partial write without arming the timer. An OUT 0 alone cannot
resynchronize an incomplete duration: all 256 values are valid parameter bytes.
Reset does not change any deadline or stopwatch, or touch the shared channel.
`0xFF` is a valid stopwatch data byte, not an error marker within a valid latch.

Do not interleave port-23 sequences between an app and an interrupt handler.
Finish a duration or drain a latch before handing the port to another user.
Dedicated operations work while the shared channel is collecting, BUSY,
returning a response, or in ERROR, and do not poll the worker or alter its frame.
Framed timer calls share the deadlines/stopwatches, but not the dedicated
interface's selector, partial duration or response latch.

For a seconds delay in BASIC:

```basic
10 OUT 23,19:OUT 23,1:OUT 23,35
20 FOR T=1 TO 1000000
30 S=INP(23)
40 IF S=0 THEN GOTO 100
50 IF S<>1 THEN GOTO 900
60 NEXT T
70 GOTO 900
100 PRINT "Expired":END
900 S=INP(23):OUT 23,0:PRINT "Timer error/timeout":END
```

The dcc `svc_start_timer`, `svc_timer_active` and `svc_access_stopwatch` helpers retain the
timer argument types and semantics; their embedded assembly uses this dedicated
interface. The header uses descriptive port/group constants and `#pragma once`.
A timer query costs one OUT
and one IN instead of at least 11 shared-service accesses. Raw clients can
select once, then use one IN per poll, or check all four timers with the mask.
Start costs three OUTs (milliseconds) or two (seconds); stopwatch reset costs
one OUT, and a read one OUT plus four INs.

## Complete old-to-new port mapping

Old ports in this table are **decimal**, as in the current C router and
[existing reference](01-i8080-IO-Ports.md). New command/operation bytes are hex.
For shared services, write the command to `0x02`, send the framed payload on
`0x03`, and read status on `0x02` / framed results on `0x04`.

Timer rows retain the framed compatibility mapping. Prefer the dedicated
port above for new countdown/stopwatch clients.
`SET(id, duration)` below means timer command `0x10`, payload
`[0x00, id, duration:u32]`; `QUERY(id)` means payload `[0x01, id]`.
Responses in this table exclude the common length prefix.

| Old port | Existing direction and behavior | New service / payload or physical port | New result / migration note |
|----------|---------------------------------|----------------------------------------|-----------------------------|
| 24 | OUT timer 0 high byte; IN running | `0x10 SET(0, duration)` / `QUERY(0)` | Combine old high/low into one duration; query returns 0 expired/unset, 1 running |
| 25 | OUT timer 0 low byte/start; IN running | Same timer 0 operations | One START replaces both OUTs |
| 26 | OUT timer 1 high byte; IN running | `0x10 SET(1, duration)` / `QUERY(1)` | Same as timer 0 |
| 27 | OUT timer 1 low byte/start; IN running | Same timer 1 operations | One START replaces both OUTs |
| 28 | OUT timer 2 high byte; IN running | `0x10 SET(2, duration)` / `QUERY(2)` | Preserve third timer |
| 29 | OUT timer 2 low byte/start; IN running | Same timer 2 operations | One START replaces both OUTs |
| 30 | OUT seconds countdown; IN running | `0x10 SET(3, seconds)` / `QUERY(3)` | Preserve separate seconds timer and its whole-second clock |
| 31 | OUT local DATE-style time | `0x20 [0x04]` | Same formatted text, without transport NUL |
| 37 | OUT stopwatch 0 selector 0/1/2 | `0x11 [selector, 0]` | Reset / seconds:u32 / milliseconds:u32 |
| 38 | OUT stopwatch 1 selector 0/1/2 | `0x11 [selector, 1]` | Same, stopwatch 1 |
| 39 | OUT stopwatch 2 selector 0/1/2 | `0x11 [selector, 2]` | Preserve third stopwatch |
| 41 | OUT boot seconds text | `0x20 [0x00]` | Decimal boot seconds text, unchanged units |
| 42 | OUT UTC ISO date/time | `0x20 [0x01]` | UTC ISO text |
| 43 | OUT local ISO date/time | `0x20 [0x02]` | Local ISO text using configured timezone |
| 44 | OUT long local date | `0x20 [0x03]` | Long local date text |
| 45 | OUT random request | `0x21 [0x00]` | Two random bytes, low then high |
| 46 | OUT weather field selector 0-14 | `0x40 [0x00, field]` | Selected field text, including fields absent from the old docs |
| 47 | IN weather status | `0x40 [0x01]` | One byte: 0 no data, 1 fetching, 2 ready, 3 error |
| 48 | OUT hostname/IP/device ID selector 0/1/2 | `0x21 [0x01, selector]` | Same selected text |
| 49 | OUT reboot magic `0xA5` | `0x21 [0x02, 0xA5]` | COMPLETE, then deferred safe supervisor restart |
| 52 | OUT interrupt rate; IN configured rate | **OUT/IN `0x07`** | Same single-byte rate, 0 disables, 1-255 Hz |
| 60 | OUT FT command; IN FT status | **OUT/IN `0x15`** | Preserve FT command/status values |
| 61 | OUT filename; IN counted chunk | **OUT/IN `0x16`** | Preserve FT filename/chunk protocol |
| 70 | OUT firmware version request | `0x21 [0x03]` | Firmware version text |
| 71 | OUT ENV operation 0-9; IN last ENV status | `0x30 [operation, parameters...]` | See ENV table; common status/errors replace `IN 71` |
| 72 | OUT append ENV request byte | Parameters inside the same `0x30` transaction | No separate global ENV request-building port |
| 120 | OUT resets chat request; **IN submits it** | `0x50 [0x00]` / `0x50 [0x03]` | Separate request reset and submit operations |
| 121 | OUT append chat request byte | `0x50 [0x01, text bytes...]` | Append a bounded block; see chat operations |
| 122 | OUT reset chat response state | `0x50 [0x02]` | Explicit chat response reset, not transport RESET |
| 123 | IN chat stream status | `0x50 [0x04]` | One byte: 0 EOF, 1 waiting, 2 data ready |
| 124 | IN next chat response byte | `0x50 [0x05, maximum:u16]` | Framed counted chunk plus stream status |
| 200 | IN common response buffer | **IN `0x04`**, after RESPONSE_READY | Read length then payload; never read to NUL for binary results |

This accounts for all **32 distinct legacy ESP32 service-port numbers**,
including the header-defined ENV and interrupt constants. The local/Docker
Sense HAT extension adds the 18 legacy selectors documented below. Terminal,
disk and front-panel ports are intercepted outside that router and are not
service migrations.

## Group operation details

### Countdown timers: command `0x10`

Synchronous compatibility interface; dedicated port `0x17` accesses the same
countdowns without framing or a worker handoff.

| Operation | Request payload | Result |
|-----------|-----------------|--------|
| `0x00` START | `[0x00, id, duration:u32]` | COMPLETE once armed |
| `0x01` QUERY | `[0x01, id]` | One byte: 1 running, 0 expired/unset |

IDs 0-2 preserve the three millisecond timers, with durations 0-65535 ms.
ID 3 preserves the seconds timer, with durations 0-255 **seconds** and the
existing whole-boot-second deadline calculation. The wire field is u32 for
consistent framing, not an implicit expansion of legacy duration ranges.
Reject invalid IDs/ranges. Zero duration retains existing immediate-expiry
behavior. High-byte writes no longer partially reconfigure a running timer;
the complete START transaction replaces the target atomically.

### Stopwatches: command `0x11`

Synchronous compatibility interface; dedicated port `0x17` accesses the same
stopwatches without framing or a worker handoff.

| Operation | Request payload | Result |
|-----------|-----------------|--------|
| `0x00` START/RESET | `[0x00, id]` | COMPLETE |
| `0x01` READ_SECONDS | `[0x01, id]` | u32 elapsed seconds |
| `0x02` READ_MS | `[0x02, id]` | u32 elapsed milliseconds |

IDs 0-2 remain independent. Preserve passive counters, initial boot-relative
behavior before first reset, and modulo-2^32 results (milliseconds wrap at
about 49.7 days). **Old stopwatch data is big-endian; new results are
little-endian.** SDKs must decode bytes explicitly rather than copying them
into a BDS C `long`.

### Clock: command `0x20`

Operations `0x00`-`0x04` are listed in the mapping table and take no additional
parameters. Preserve each existing text format and boot-seconds units.
For calendar operations, return error `0x15` when the clock is unsynchronized;
do not pass the old `+<seconds>s` fallback off as a valid date. This is an
intentional behavior change required by the ROMless service contract.

### Utilities: command `0x21`

Operations and parameters are listed in the mapping table. Reject unknown
system-info selectors and incorrect reboot confirmation explicitly, rather
than returning `"unknown"` or silently ignoring the request.

The reboot operation must not reset the supervisor inside the port handler.
After reporting COMPLETE, schedule the restart outside the trap; on physical
hardware stop/reset the Z80 and isolate its data path first. ACK is not a
second confirmation. Retain application-level user confirmation before sending
the `0xA5` request. Version text identifies the actual supervisor/firmware;
a future Pico implementation must not claim to be ESP32 firmware.
The host has no supervisor-reboot implementation and explicitly reports
UNAVAILABLE for a correctly confirmed reboot request.

### Environment: command `0x30`

Keep the legacy operation numbers, but make each request self-contained.
Here `key`/`value`/`line` mean raw text bytes without a trailing NUL.

| Operation | Request payload | Successful result |
|-----------|-----------------|-------------------|
| `0x00` RESET | `[0x00]` | COMPLETE; clear any driver-side ENV request state |
| `0x01` INIT | `[0x01]` | COMPLETE |
| `0x02` GET | `[0x02, key_length, key...]` | Value text; zero-length value is valid |
| `0x03` SET | `[0x03, key_length, key..., value_length:u16, value...]` | COMPLETE |
| `0x04` DELETE | `[0x04, key_length, key...]` | COMPLETE |
| `0x05` LIST | `[0x05]` | Existing formatted key/value list text |
| `0x06` COUNT | `[0x06]` | Decimal entry count text, preserving the old result |
| `0x07` CLEAR | `[0x07]` | COMPLETE |
| `0x08` EXISTS | `[0x08, key_length, key...]` | COMPLETE if present; NOT_FOUND error otherwise |
| `0x09` EXECUTE | `[0x09, line...]` | Existing CLI result text, possibly empty |

Preserve key normalization, 16-character key limit, 255-character value limit,
and current CLI semantics. Reject embedded NULs and malformed/inconsistent
lengths; validate before storage changes. CLI lines are limited to 511 bytes
by the current request capacity. LIST capacity overflow must be explicit,
not silent truncation.

Old signed status values on `IN 71` map to common errors: -1 (`0xFF`) open
becomes `0x10`; -2 (`0xFE`) read becomes `0x11`; -3 (`0xFD`) write becomes
`0x12`; -4 (`0xFC`) full becomes `0x13`; -5 (`0xFB`) missing becomes `0x14`.
Old success 0 becomes COMPLETE or RESPONSE_READY, depending on the operation.
An empty value is not a missing variable.

### Weather: command `0x40`

`[0x00, field]` retrieves a field; `[0x01]` retrieves weather-service status.
Field numbers remain: 0 city, 1 current main, 2 current description,
3 temperature, 4 humidity, 5 wind, 6 forecast main, 7 forecast description,
8 forecast temperature, 9 forecast valid-at time, 10 last-fetch age in seconds,
11 units, 12 last error, 13 current feels-like temperature, and 14 forecast
feels-like temperature. Fields 11-14 were implemented by the drivers but
omitted from the old main port-reference table.

The returned weather status is a **payload byte**, not a replacement for the
shared transaction status. The status query can succeed while reporting
weather state 3 (fetch error). Weather readings require cached data; report
unavailable when absent, including during an initial fetch. Units and error
diagnostics remain readable without cached measurements. Cached readings
remain readable during refresh or a later fetch error. Preserve configuration
and background refresh behavior.

### Chat: command `0x50`

| Operation | Request payload | Successful result |
|-----------|-----------------|-------------------|
| `0x00` RESET_REQUEST | `[0x00]` | COMPLETE; empty the assembled request |
| `0x01` APPEND | `[0x01, text bytes...]` | COMPLETE; append to assembled request |
| `0x02` RESET_RESPONSE | `[0x02]` | COMPLETE; preserve existing generation/reset semantics |
| `0x03` SUBMIT | `[0x03]` | COMPLETE once queued, not once network work finishes |
| `0x04` STATUS | `[0x04]` | One stream-status byte: 0 EOF, 1 waiting, 2 data ready |
| `0x05` READ_CHUNK | `[0x05, maximum:u16]` | `[stream_status, count:u16, bytes...]` |

APPEND blocks contain at most 2047 bytes after the operation byte; accumulate
them within the existing chat request capacity, reserving space for its local
NUL. The client omits the legacy terminating NUL on the wire. SUBMIT explicitly
performs what **old `IN 120`** does, including resetting response generation;
it does not implicitly clear the assembled request.

READ_CHUNK accepts a maximum of 1-256 bytes and never waits for network data.
Return a positive count and status 2 if bytes are available; return count zero
and status 1 when waiting, or count zero and status 0 at EOF. Deliver buffered
bytes before reporting EOF. Each chunk is its own shared transaction, allowing
timer, ENV and other service requests between chat reads. A chunk consumes
bytes just as old `IN 124` did; it is not a second copy of the stream.

Maintain a background chat job and bounded buffering independently of the
shared transport. ESP32 retains queue back-pressure; the host retains its
explicit response-truncation marker if its ring fills. Reject a second SUBMIT
while that job is active.
Do not mistake queue acceptance for a completed chat response. An unavailable
chat implementation reports an explicit error, not EOF-shaped success.

### Sense HAT: command `0x60` (local/Docker only)

Send `[selector, data]`, where the selector retains the old decimal port
number as an **operation byte**, not as a physical port address:

| Legacy port / new selector | Function |
|----------------------------|----------|
| 63 | Read onboard sensor selected by `data`; return text |
| 65 | LED panel color |
| 80 | Panel mode |
| 81 | Font color |
| 85 | Display character |
| 90-97 | Bitmap rows 0-7 |
| 98 | Pixel on |
| 99 | Pixel off |
| 100 | Pixel flip |
| 101 | Clear pixels |
| 102 | Draw bitmap |

Panel writes return COMPLETE. Sensor reads return a framed text response.
The panel must be compiled in, enabled and present; otherwise return
UNAVAILABLE, including on ESP32 and the dcc adapter. These operations do not
consume a further physical port. The existing host-only numeric port routes
and response port 200 are removed.

## Dedicated protocols

### Interrupt timer: physical port `0x07`

- `OUT 0x07, rate`: 0 disables; 1-255 sets Hz and clears pending timer requests.
- `IN 0x07`: configured rate, not a pending-interrupt flag.
- Preserve the current interrupt provider and acknowledge behavior in the
  emulator. Do not route this through the shared transport or clear its response.
- Assigning a port does not add interrupt wiring to the physical SBC. A physical
  implementation still needs a supported Z80 INT/acknowledge path; if absent,
  interrupt-driven samples are not supported there. Do not silently substitute
  a polling timer for interrupts.

### File transfer: physical ports `0x15/0x16`

Only the port addresses change; retain the existing download protocol:

| New access | Value / meaning |
|------------|-----------------|
| `OUT 0x15` | 0 NOP, 1 SET_FILENAME, 2 legacy filename marker (no-op), 3 REQUEST_CHUNK, 4 CLOSE |
| `OUT 0x16` | Filename bytes followed by NUL; current maximum 127 characters |
| `IN 0x15` | 0 idle, 1 data ready, 2 EOF, 3 busy, `0xFF` error |
| `IN 0x16` | Leading count byte, then that many raw bytes; count 0 means **256**, not EOF |

Poll status before reading a chunk. Drain the counted payload before requesting
another; zero bytes inside file data do not terminate it. Preserve file offset,
close and EOF handling. Do not apply shared u16 framing or shared status values
to these dedicated ports. This does not add uploads or implement file transfer
on the Pico by allocating addresses.

## Source/documentation differences found

- The current chat documentation labels `OUT 120` as triggering a request and
  `IN 120` as an acknowledgement. In `chat_io.c`, OUT actually clears the
  request, and **IN actually submits it**. This mapping follows the code.
- Interrupt timer port 52 is routed by `io_ports.c` and defined by
  `interrupt_timer.h`, but omitted from the main port-reference tables.
  See [Z80 interrupts](02-Z80-interrupts.md).
- Port 200 is not universally NUL-delimited: random numbers require exactly
  two bytes and stopwatches exactly four, including embedded zeros.
- The ROMless examples describe two timers/two stopwatches. This project has
  three millisecond timers, a separate seconds timer and three stopwatches;
  all are retained here.
- The ROMless command/status example values are illustrative. These grouped
  commands and length-prefixed frames need implementation on both sides;
  allocating the addresses does not make the examples binary-compatible.

## Phase-two and phase-three checklist

Phase two shares `port_drivers/io_ports.c` and `io_service.c` across the firmware,
`altair_local` (also used by Docker), and the dcc debug host adapter. The former
host router mirrors are removed. CPU terminal/disk intercepts remain unchanged.
Internal legacy driver selectors remain private implementation details, not
decoded or externally accessible port aliases.

`io_ports_init()` starts the worker after platform drivers are initialized and
before CPU execution. Host shutdown joins it before closing the drivers.
The public transport constants and enums are in `port_drivers/io_ports.h`.

Validate every legacy row above, all operation selectors, fragmented requests,
invalid lengths/IDs, busy rejection, ACK/RESET, late worker completions,
embedded-zero binary values, empty/missing ENV values, unsynchronized clocks,
and chat interleaving/back-pressure. Test the exact 2048-byte transport limit,
the 256-byte FT count-zero convention, and absence of changes to fixed ports.

Phase three adds the stateless `Apps/COMMON/DXSVC.C` transport. Current dcc
builds compile it as a shared source. Maintained clients cover:

- dcc games/tools: `BREAKOUT`, `SNAKE`, `CLOCK`, `MANC11`, `HANGMAN`,
  `TETRIS`, `SHEETS`, `ATTNC11`, `DCCINT`, `DATE` and `CHAT`.
- Hand-written assembly: `DATE`, `SLEEP`, `INTTEST`, `ATTNINF`, `ATTNZ80`,
  `ENV`, `ESP32` and the dedicated-port `FT` bootstrap.
- Root BASIC: `COUNT.BAS`, `TIME.BAS`, `WSPERF.BAS` and `FONT.BAS`.
  The hand-written `SHEETS.ASM` has no service I/O to change.

`JSON.C` and `JSON.BAS` remain explicitly **non-runnable historical samples**:
their Azure Sphere data/publishing APIs have no service equivalent.
The obsolete GF HTTP sample was retired, not assigned a new service. Its
former OUT ports 109/110/112/114 and IN ports 33/201 had no backend handlers
in the baseline router; allocating FT ports does not implement arbitrary HTTP
or GitHub downloads. The superseded BDS/MANC89 Mancala and assembly WSPERF
variants were also retired; MANC11 and the root BASIC benchmark are retained.

Validation includes real dcc, MAC/LOAD and M80/L80 builds, client framing/
error/byte-order fixtures, and real MBASIC/MCP stopwatch/timer/clock and
error-reset recovery checks. See the shared client reference for commands.

Rebuild/install application binaries and BASIC files alongside a driver
upgrade. In particular, the MCP pristine tools disk contains FT for ports
21/22, because an old FT binary cannot bootstrap itself through removed
ports 60/61. Application media layout is a deployment choice; do not overwrite
user-populated disk images or assume a nearly-full image can hold larger
recompiled clients. Terminal/disk BIOS interfaces remain unchanged.
