# ROMless service I/O reference

ESP32 and the Altair DCC debug adapter compile the same
[`io_ports.c`](https://github.com/gloveboxes/esp32-altair-8800/blob/main/port_drivers/io_ports.c)
transport. Platform drivers supply the service backends. The local SBC emulator
and Apple container use the SBC adapter and its
[I/O port map](https://gloveboxes.github.io/Z80ROMlessSBC/system/io-ports/).

## Address decoding and allocation

The service router decodes `address & 0x17`, giving 16 canonical addresses.
Use canonical addresses in programs. The
[physical ROMless allocation](https://gloveboxes.github.io/Z80ROMlessSBC/system/io-ports/)
reserves terminal, disk and diagnostic addresses:

| Hex | Decimal | Physical ROMless allocation |
|-----|---------|-----------------------------|
| `0x00` | 0 | Terminal data |
| `0x01` | 1 | Terminal status |
| `0x02` | 2 | Shared service command (OUT) / status (IN) |
| `0x03` | 3 | Shared length-prefixed request (OUT) |
| `0x04` | 4 | Shared length-prefixed response (IN) |
| `0x05` | 5 | Reserved BIOS/qualification diagnostics |
| `0x06` | 6 | Reserved diagnostic data |
| `0x07` | 7 | Interrupt timer rate (OUT/IN) |
| `0x10` | 16 | Disk command/status |
| `0x11` | 17 | Disk drive selection |
| `0x12` | 18 | Disk sector address low |
| `0x13` | 19 | Disk sector address high |
| `0x14` | 20 | Disk data |
| `0x15` | 21 | Available |
| `0x16` | 22 | Available |
| `0x17` | 23 | Dedicated countdown/stopwatch interface |

### Emulator versus physical hardware

The emulator's CPU adapter intercepts terminal ports `0x00/0x01` and
`0x10/0x11`, disk ports `0x08/0x09/0x0A`, and front-panel input `0xFF`
before forwarding other ports to the service router. These intercepts
support the supplied CP/M disks. Do not apply `& 0x17` globally: that would
alias emulator disk ports onto terminal/service ports.

## Shared transport conventions

Command bytes identify groups, not physical ports. Group requests begin
with an operation byte. All `u16`/`u32` values use little-endian wire order.
The [CP/M SDK reference](https://github.com/gloveboxes/esp32-altair-8800/blob/main/Apps/COMMON/README.md)
describes the C helpers.

| Command | Group |
|---------|-------|
| `0x00` | ACK: acknowledge COMPLETE or a fully consumed error |
| `0x01` | RESET: abandon/reset the shared transport |
| `0x02` | IDENTIFY: return `ROMLESS-SVC/1` |
| `0x10` | Countdown timers |
| `0x11` | Stopwatches |
| `0x20` | Clock/date/uptime |
| `0x21` | Utilities/system information/reboot |
| `0x30` | Persistent environment variables and CLI |
| `0x40` | Weather |
| `0x50` | Chat |
| `0x60` | Host Sense HAT panel/sensors |
| `0x70` | File transfer |

### Framing and status

1. Wait for READY on `IN 0x02`.
2. Write the group to `OUT 0x02`.
3. Write a u16 payload length to `OUT 0x03`, then exactly that many payload
   bytes. Execution starts after the final byte.
4. Poll `IN 0x02` with a bounded timeout.
5. On RESPONSE_READY, read a u16 length from `IN 0x04`, then that many
   response bytes. Consuming the full frame returns READY.
6. On COMPLETE, write ACK to `OUT 0x02`.
7. On ERROR, consume its framed error code and ACK, or RESET.

ACK, RESET and IDENTIFY execute immediately without request framing.
IDENTIFY requires READY and returns a normal framed response.

| Status | Meaning |
|--------|---------|
| `0x00` | READY |
| `0x01` | COMPLETE; ACK required |
| `0x02` | RESPONSE_READY |
| `0x03` | COLLECTING request length/payload |
| `0x04` | BUSY |
| `0xFF` | ERROR; framed error available |

Status polling neither acknowledges completion nor consumes payload.
Text excludes its terminating NUL; binary zero is ordinary data.
The shared payload limit is 2048 bytes, excluding the two length bytes.
Do not truncate a request or response and report success.

Only one shared transaction may be active. A group command during an active
transaction produces a busy/state error and requires RESET. ACK is valid
only for COMPLETE or a fully consumed error. RESET discards partial frames
and invalidates late worker completions. A worker-backed request can remain
BUSY while an abandoned job finishes.

Common error codes are `0x01` unknown group, `0x02` unknown operation,
`0x03` invalid length, `0x04` invalid parameter, `0x05` unavailable,
`0x06` busy, `0x07` timeout, `0x08` invalid state, `0x09` response underflow,
`0x10` storage open, `0x11` storage read, `0x12` storage write,
`0x13` capacity, `0x14` not found, `0x15` unsynchronized clock,
`0x16` invalid reboot confirmation, `0x20` missing HTTP base URL,
and `0x21` transfer failure. Error codes are not port addresses.

### Execution ownership

Calendar, environment, weather, chat and utility transactions use a worker:
Core 0 on ESP32, a background thread on hosts. The CPU polls completion
without blocking on network or flash operations. Timer groups are
synchronous CPU-thread operations sharing state with port `0x17`.

FT has its own backend. ESP32 uses queued Core 0 TCP work; the DCC adapter
reads the configured filesystem. FT reads occupy the
shared transaction until their response is consumed. Between reads, other
shared services can run without closing the FT session. Interrupt rate and
dedicated timer operations remain independent of shared transactions.

`io_ports_init()` starts the shared worker after platform initialization;
shutdown stops it and closes FT. Response generation tags prevent abandoned
jobs from replacing a current response. ESP32 FT queue replies own their
payload until the CPU consumes them.

## Dedicated timer port: `0x17` (decimal 23)

All countdown and stopwatch commands, parameters and results use port 23,
without framing, BUSY polling or ACK.

| OUT command | Following OUT bytes | IN result |
|-------------|---------------------|-----------|
| `0x00` | None | Reset interface; countdown running mask |
| `0x10`-`0x12` | Duration u16 | Start millisecond timer 0-2; running mask |
| `0x13` | Duration u8 | Start seconds timer 3; running mask |
| `0x20`-`0x23` | None | Selected countdown: 1 running, 0 expired/unset |
| `0x30` | None | Running mask, bits 0-3 |
| `0x40`-`0x42` | None | Reset stopwatch 0-2; running mask |
| `0x50`-`0x52` | None | Stopwatch seconds, u32 |
| `0x60`-`0x62` | None | Stopwatch milliseconds, u32 |

Timers 0-2 accept 0-65535 ms; timer 3 accepts 0-255 seconds. Zero expires
immediately. A complete duration replaces its timer atomically. Stopwatch
reads latch all four bytes at command time; values wrap modulo 2^32.
Before first reset, stopwatches measure time since boot.

Finish parameter writes and drain all four stopwatch bytes before another
user accesses this interface. Invalid commands, interrupted parameters and
response overreads enter an error state (`IN` returns `0xFF`). Recover with
one `IN 23`, then `OUT 23,0`; this does not change timers or the shared
channel. `0xFF` within a valid stopwatch response is ordinary data.

The SDK timer helpers use this interface. Groups `0x10/0x11` provide framed
access to the same timers/stopwatches with separate transaction state.

## Group operation details

### Countdown timers and stopwatches: `0x10/0x11`

Countdown START is `[0, id, duration:u32]`, returning COMPLETE; QUERY is
`[1, id]`, returning one running byte. IDs and ranges match port `0x17`.
Stopwatch requests are `[operation, id]`: 0 reset (COMPLETE), 1 read seconds
(u32), 2 read milliseconds (u32). IDs are 0-2.

### Clock and utilities: `0x20/0x21`

Clock requests contain one operation byte: 0 boot seconds, 1 UTC ISO time,
2 local ISO time, 3 long local date, 4 DATE-style local time. Results are
text. Calendar requests fail explicitly when the clock is unsynchronized.

Utility requests are `[0]` for two random bytes; `[1, selector]` for hostname
(0), IP (1), or device ID (2); `[2, 0xA5]` for confirmed reboot; and `[3]`
for firmware version text. ESP32 schedules reboot outside the CPU handler
after COMPLETE. Host builds report UNAVAILABLE for reboot.

### Environment: `0x30`

Keys, values and CLI lines are raw text without a terminating NUL.

| Request payload | Successful result |
|-----------------|-------------------|
| `[0]` RESET | COMPLETE; clear ENV request state |
| `[1]` INIT | COMPLETE |
| `[2, key_length, key...]` GET | Value text |
| `[3, key_length, key..., value_length:u16, value...]` SET | COMPLETE |
| `[4, key_length, key...]` DELETE | COMPLETE |
| `[5]` LIST | Formatted key/value text |
| `[6]` COUNT | Decimal entry count text |
| `[7]` CLEAR | COMPLETE |
| `[8, key_length, key...]` EXISTS | COMPLETE if present; NOT_FOUND otherwise |
| `[9, line...]` EXECUTE | CLI result text, possibly empty |

Requests are self-contained and reject embedded NULs and inconsistent lengths.
Keys are normalized and limited to 16 characters, values to 255, and CLI
lines to 511. LIST overflow is an explicit capacity error.
Empty values are valid; missing keys return NOT_FOUND.

### Weather: `0x40`

`[0, field]` returns field text; `[1]` returns weather-service status:
0 no data, 1 fetching, 2 ready, 3 error. This payload byte is independent
of shared transaction status.

Fields are 0 city, 1 current main, 2 current description, 3 temperature,
4 humidity, 5 wind, 6 forecast main, 7 forecast description,
8 forecast temperature, 9 forecast valid-at time, 10 last-fetch age
(seconds), 11 units, 12 last error, 13 current feels-like temperature,
14 forecast feels-like temperature.

Cached readings remain available during refresh or a subsequent fetch error.
Missing readings report UNAVAILABLE. Units/error diagnostics are readable
without cached measurements.

### Chat: `0x50`

| Request payload | Successful result |
|-----------------|-------------------|
| `[0]` RESET_REQUEST | COMPLETE; empty the assembled request |
| `[1, text bytes...]` APPEND | COMPLETE |
| `[2]` RESET_RESPONSE | COMPLETE |
| `[3]` SUBMIT | COMPLETE once queued |
| `[4]` STATUS | Stream-status byte: 0 EOF, 1 waiting, 2 data ready |
| `[5, maximum:u16]` READ_CHUNK | `[stream_status, count:u16, bytes...]` |

APPEND accepts up to 2047 text bytes per transaction within the backend's
request capacity, reserving room for its internal NUL. SUBMIT resets response
generation but preserves the assembled request. Reject a second SUBMIT
while a chat job is active. Completion is queue acceptance, not network
completion.

READ_CHUNK accepts a maximum of 1-256 and does not wait for network data.
Available bytes have status 2 and a positive count; waiting/EOF have zero
count and status 1/0. Deliver buffered bytes before EOF. Each chunk is a
separate transaction, allowing other services between reads. ESP32 applies
queue back-pressure; host ring overflow has an explicit truncation marker.
An unavailable backend reports an error, not EOF-shaped success.

### Sense HAT: `0x60`

Requests are `[selector, data]`: selectors 63 sensor read, 65 panel color,
80 mode, 81 font color, 85 character, 90-97 bitmap rows, 98 pixel on,
99 pixel off, 100 pixel flip, 101 clear, 102 draw. These are operation
bytes, not ports. Sensor selection uses `data` and returns text; panel
writes return COMPLETE. Unsupported/absent hardware returns UNAVAILABLE,
including on ESP32 and the DCC adapter.

### File transfer: `0x70`

| Request payload | Successful result |
|-----------------|-------------------|
| `[1, filename bytes...]` | Open/replace session; COMPLETE, then ACK |
| `[3]` | `[state, up to 256 file bytes...]` |
| `[4]` | Cancel/close; COMPLETE, then ACK |

Filenames contain 1-127 bytes without a terminating NUL. Read and close
accept no extra bytes. State 0 means more data; 2 means EOF, optionally
with final bytes. Empty EOF has payload `[2]` and length 1; a full chunk
has length 257. Consume the entire framed response before another request.
Repeated EOF reads return empty EOF. Transfer failures return error `0x21`.
RESET during a BUSY read cancels the FT session; close also invalidates
pending replies and buffered data. Other service transactions between FT
reads preserve its filename and offset.

ESP32 connects to its configured remote FT server on TCP port 8090.
The DCC adapter uses the session's native files root. This shared ABI also supports HTTP(S) hosts:
error `0x20` means their required `HTTP_BASE_URL` is missing.

## Interrupt timer: physical port `0x07`

`OUT 0x07` sets 0 to disable or 1-255 Hz and clears pending timer requests.
`IN 0x07` returns the configured rate, not an interrupt-pending flag.
The emulator's interrupt provider/acknowledgement path is independent of
shared transactions. A physical implementation requires an INT/acknowledge
path; a port assignment alone does not provide interrupt wiring.

## Validation

```sh
cmake -S dcc_debug_io_adapter -B dcc_debug_io_adapter/build
cmake --build dcc_debug_io_adapter/build
ctest --test-dir dcc_debug_io_adapter/build --output-on-failure
```

The adapter tests cover service framing, invalid requests, ACK/RESET,
abandoned replies and error recovery.
