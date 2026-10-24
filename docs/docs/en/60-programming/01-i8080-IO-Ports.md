The Intel 8080 can address 256 input ports and 256 output ports. Programs
access them with the `IN` and `OUT` instructions. This page documents the
current port assignments used by the Altair emulator.

## Intel 8080 IO ports and peripherals

![The image shows an example of Intel 8080 IO port usage](img/i8080_ports.png)

## Port assignments

The emulator decodes the port address with `address & 0x17`. Use the canonical
addresses in this table in programs and documentation.

| Address | Decimal | Direction | Purpose |
|---------|---------|-----------|---------|
| `0x02` | 2 | IN / OUT | Shared-service status and command |
| `0x03` | 3 | OUT | Shared-service request length and payload |
| `0x04` | 4 | IN | Shared-service response length and payload |
| `0x07` | 7 | IN / OUT | Interrupt timer rate |
| `0x15` | 21 | IN / OUT | File-transfer command and status |
| `0x16` | 22 | IN / OUT | File-transfer filename and data |
| `0x17` | 23 | IN / OUT | Countdown timers and stopwatches |

Terminal and disk ports are handled by the emulator's existing console and
disk interfaces.

## Shared services on ports `0x02`-`0x04`

Write a service group to `OUT 0x02`, then write a two-byte payload length to
`OUT 0x03` (low byte first), followed by exactly that many payload bytes.
The first payload byte is the operation. Poll `IN 0x02` until the request
finishes. For a response, read its two-byte length from `IN 0x04` (low byte
first), then read exactly that many bytes. A response is binary-safe: zero is
ordinary data and text responses do not include a trailing NUL. For a command
with no response, acknowledge COMPLETE by writing `0x00` to `OUT 0x02`. On
ERROR, read the complete error frame and acknowledge it; write RESET (`0x01`)
to `OUT 0x02` to abandon a partial request or recover an unknown state.

| Group byte | Service |
|------------|---------|
| `0x10` | Countdown timers |
| `0x11` | Stopwatches |
| `0x20` | Clock, date and uptime |
| `0x21` | Random numbers, system information and reboot |
| `0x30` | Environment variables and CLI commands |
| `0x40` | Weather |
| `0x50` | Chat |
| `0x60` | Sense HAT on supported host builds |

Status values from `IN 0x02` are `0x00` READY, `0x01` COMPLETE, `0x02`
RESPONSE_READY, `0x03` COLLECTING, `0x04` BUSY and `0xFF` ERROR. A framed
error response contains an error code as its first payload byte. The shared
payload limit is 2048 bytes. Only one shared transaction may be active at a
time. Poll with a bounded timeout. See the [dcc client API](https://github.com/gloveboxes/esp32-altair-8800/blob/main/Apps/COMMON/README.md)
for C helpers and service operation details.

### dcc C example

The shared dcc client provides typed functions for common operations. This
example starts a 100 ms timer and waits for it to expire:

```c
#include "DXSVC.H"

int main(void)
{
    svc_start_timer(1, 100);
    while (svc_timer_active(1))
        ;
    return 0;
}
```

The same client provides `svc_random_u16()` and `svc_access_stopwatch()`.
Use `svc_call_service()` or `svc_operation_send()` for other framed requests.

## Countdown timers and stopwatches on `0x17`

All timer operations use `OUT 0x17` for a command and its parameters, and
`IN 0x17` for results. This dedicated interface does not use shared-service
framing or acknowledgements.

| OUT command | Following OUT bytes | IN result |
|-------------|---------------------|-----------|
| `0x00` | None | Reset interface; return to running-mask mode |
| `0x10`-`0x12` | Duration u16, little-endian | Start millisecond timer 0-2 |
| `0x13` | Duration u8 | Start seconds timer 3 |
| `0x20`-`0x23` | None | Select timer 0-3; read `1` while running, `0` when expired |
| `0x30` | None | Read running mask; bits 0-3 represent timers 0-3 |
| `0x40`-`0x42` | None | Reset stopwatch 0-2 |
| `0x50`-`0x52` | None | Read stopwatch seconds as four little-endian bytes |
| `0x60`-`0x62` | None | Read stopwatch milliseconds as four little-endian bytes |

Timer IDs 0-2 accept durations from 0 to 65535 milliseconds. Timer 3 accepts
0 to 255 seconds. A zero duration expires immediately. Stopwatch results are
unsigned 32-bit values; read all four bytes after selecting a stopwatch.

Example 8080 assembly starts timer 1 for 100 ms and polls it:

```asm
        MVI A,11H      ; select timer 1 start
        OUT 23
        MVI A,64H      ; duration low byte: 100 ms
        OUT 23
        MVI A,00H      ; duration high byte
        OUT 23
        MVI A,21H      ; select timer 1 query
        OUT 23
WAIT:   IN 23
        CPI 01H
        JZ WAIT
```

Finish a multi-byte command or read all four stopwatch bytes before another
port-`0x17` user accesses the interface. To recover an unknown parser state,
read `0x17` once and then write `0x00` to it. This resets the interface without
changing running timers or stopwatch values.

## Interrupt timer on `0x07`

- `OUT 0x07`: set the interrupt rate in hertz; zero disables it.
- `IN 0x07`: read the configured rate.

This port reports the rate, not a pending-interrupt flag.

## File transfer on `0x15` and `0x16`

The `FT` CP/M application uses these dedicated ports to download files.

| Access | Value | Meaning |
|--------|-------|---------|
| `OUT 0x15` | `0` | No operation |
| `OUT 0x15` | `1` | Start a filename transfer |
| `OUT 0x15` | `2` | Filename marker |
| `OUT 0x15` | `3` | Request the next data chunk |
| `OUT 0x15` | `4` | Close the transfer |
| `OUT 0x16` | Filename bytes, then NUL | Set the filename; maximum 127 characters |
| `IN 0x15` | `0`, `1`, `2`, `3`, `0xFF` | Idle, data ready, EOF, busy, error |
| `IN 0x16` | Count byte, then data | Read that many bytes; count `0` means 256 bytes |

Poll status on `IN 0x15` before reading a chunk. Read the count byte from
`IN 0x16`, then consume exactly that many bytes. File data may contain zero
bytes.

## Reboot

The `ENV` CP/M application provides a reboot menu option with user
confirmation. Reboot requests use utility group `0x21`, operation `0x02`, and
confirmation byte `0xA5`, sent through the shared-service protocol.
