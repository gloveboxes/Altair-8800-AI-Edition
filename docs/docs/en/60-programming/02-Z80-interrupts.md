# Z80 interrupts

The Altair emulator supports Z80 maskable interrupts in modes 0, 1, and 2.
Interrupts can wake a halted CPU, and the emulator implements `DI`, delayed
`EI`, `RETI`, `RETN`, and the Z80 interrupt flip-flops IFF1 and IFF2.

There are two sides to an interrupt:

- An **interrupt provider** in the emulator firmware raises a request. A timer
  is one provider; a GPIO, serial device, or other peripheral could be another.
- A **Z80 interrupt service routine (ISR)** in the CP/M application handles the
  request and returns to the interrupted code.

## Register an interrupt provider

Providers use
[`altair8800/interrupt_controller.h`](https://github.com/gloveboxes/esp32-altair-8800/blob/main/altair8800/interrupt_controller.h){:target=_blank}.
The controller has eight fixed provider slots and performs no dynamic memory
allocation. Registration order determines priority: the first registered
pending provider is presented to the Z80 first.

Register providers during emulator initialization, after
`interrupt_controller_init()` and before the CPU starts running:

```c
#include "interrupt_controller.h"

static interrupt_provider_id_t provider_id = INTERRUPT_PROVIDER_INVALID;

void example_provider_init(void)
{
    const interrupt_provider_config_t config = {
        .data_bus = 0xff,
        .poll = NULL,
        .context = NULL
    };

    if (!interrupt_controller_register(&config, &provider_id))
    {
        /* Registration failed: invalid arguments or all eight slots are used. */
        return;
    }
}
```

The `data_bus` byte is supplied during interrupt acknowledge:

- Mode 0 expects a `RST` opcode. `0xff` is `RST 7`, which enters at `0038H`.
- Mode 1 ignores the byte and always enters at `0038H`.
- Mode 2 combines the Z80 `I` register with this byte to locate the two-byte
  ISR address in the vector table.

### Raise and clear requests

Call `interrupt_controller_raise()` when the provider has an event:

```c
static void example_event_callback(void *argument)
{
    (void)argument;
    interrupt_controller_raise(provider_id);
}
```

`interrupt_controller_raise()` is safe to call from an ESP-IDF callback or
another ESP32 core. Repeated events are counted atomically. A request remains
pending while the Z80 has interrupts disabled and is consumed only after the
CPU accepts it.

Clear outstanding requests when disabling or resetting the device:

```c
void example_provider_disable(void)
{
    interrupt_controller_clear(provider_id);
}
```

Do not modify the Z80 registers from a provider callback. The callback only
raises a request; Core 1 changes the emulated CPU state at a safe batch
boundary.

### Polling providers

A host provider without an asynchronous callback can register a poll function:

```c
static void example_poll(void *context)
{
    example_device_t *device = context;

    if (example_device_take_event(device))
        interrupt_controller_raise(provider_id);
}

const interrupt_provider_config_t config = {
    .data_bus = 0xff,
    .poll = example_poll,
    .context = &device
};
```

The poll function should acknowledge or edge-detect the device event. It must
not raise a new request on every poll while a level remains asserted unless
that repeated behavior is intentional.

The ESP32 timer provider leaves `poll` null and raises from its `esp_timer`
callback. See
[`port_drivers/interrupt_timer.c`](https://github.com/gloveboxes/esp32-altair-8800/blob/main/port_drivers/interrupt_timer.c){:target=_blank}
for the complete implementation.

## Service providers in the emulator

The CPU execution loop is independent of individual providers. It calls the
controller halfway through and at the end of each 4,000-instruction production
batch:

```c
z80_execute_instructions(&cpu, 2000);
interrupt_controller_service(&cpu);
z80_execute_instructions(&cpu, 2000);
interrupt_controller_service(&cpu);
```

`interrupt_controller_service()` polls registered host providers, finds the
highest-priority pending request, and calls `z80_interrupt()`. It consumes one
event only when the Z80 accepts the interrupt.

On ESP32 this check occurs once per 2,000 emulated instructions. Terminal input
and front-panel commands retain their 4,000-instruction service cadence. There
is no interrupt-provider check in the per-instruction hot path, so registered
but idle providers do not materially affect normal Z80 performance.

### Interrupt latency

An asynchronous provider, such as the ESP32 timer, can raise a request at any
time. Core 1 presents that request to the emulated Z80 at the end of the current
2,000-instruction slice. At the measured emulator throughput, this keeps normal
request propagation below approximately 1 ms. The exact time varies with the
emulated instruction mix because Z80 instructions require different amounts of
host work.

The shorter service interval has negligible throughput cost. The ATTNC11
benchmark median changed from 9,425 ms with 4,000-instruction checks to 9,433 ms
with 2,000-instruction checks, an increase of approximately 0.09%.

This is a normal operating target, not a hard real-time guarantee. Additional
delay can come from ESP timer or FreeRTOS scheduling, a higher-priority pending
provider, and completion of the current emulated instruction. If Z80 interrupts
are disabled (`IFF1` is clear), the request remains pending until the program
enables interrupts, so that delay is not bounded by the 2,000-instruction
service interval. The Z80's one-instruction delay after `EI` also applies.

## Configure the timer provider from CP/M

The included timer provider is controlled through I/O port 52:

| Operation | Description |
|-----------|-------------|
| `OUT 52,0` | Disable the timer and clear pending requests |
| `OUT 52,n` | Generate `n` interrupts per second, where `1 <= n <= 255` |
| `IN 52` | Return the configured rate, or zero when disabled |

The timer supplies `FFH` on the interrupt data bus. The ESP32 implementation
uses `esp_timer`; the local emulator uses its monotonic host clock.

## Install a CP/M interrupt handler

A CP/M `.COM` program loads at `0100H`, but Z80 mode 1 enters at `0038H`.
Therefore a transient application must:

1. Execute `DI`.
2. Save the existing bytes at `0038H`.
3. Install a three-byte `JP ISR` stub at `0038H`.
4. Configure its interrupt provider and execute `EI`.
5. Before exiting, execute `DI`, disable the provider, and restore the saved
   vector bytes.

This abbreviated MAC assembly example installs a mode 1 handler and sleeps
until each timer interrupt arrives:

```asm
INTPRT  EQU     52
INTVEC  EQU     0038H

        ORG     0100H

START:  DI
        LDA     INTVEC         ; save CP/M's existing vector
        STA     OLDVEC
        LDA     INTVEC+1
        STA     OLDVEC+1
        LDA     INTVEC+2
        STA     OLDVEC+2

        MVI     A,0C3H         ; C3 = JP address
        STA     INTVEC
        LXI     H,ISR
        SHLD    INTVEC+1

        MVI     A,10           ; request 10 interrupts per second
        OUT     INTPRT
        DB      0EDH,056H      ; Z80 IM 1
        EI

WAIT:   HLT                    ; interrupt resumes at the next instruction
        LDA     TICKS
        CPI     50
        JNZ     WAIT

        DI
        XRA     A
        OUT     INTPRT         ; disable provider and clear pending requests
        LDA     OLDVEC
        STA     INTVEC
        LDA     OLDVEC+1
        STA     INTVEC+1
        LDA     OLDVEC+2
        STA     INTVEC+2
        RET

ISR:    PUSH    PSW            ; preserve every register used by the ISR
        LDA     TICKS
        INR     A
        STA     TICKS
        POP     PSW
        EI
        DB      0EDH,04DH      ; Z80 RETI

TICKS:  DB      0
OLDVEC: DS      3
        END
```

An ISR must be short and nonblocking. It must not call BDOS because the
interrupt may have arrived while BDOS was already executing. Preserve every
register the ISR uses, including flags through `PUSH PSW`/`POP PSW`.

The complete working example is
[`Apps/INTTEST/INTTEST.ASM`](https://github.com/gloveboxes/esp32-altair-8800/blob/main/Apps/INTTEST/INTTEST.ASM){:target=_blank}.
It counts 50 interrupts at 10 Hz, restores the original vector, and returns to
CP/M.

## Call C from an ISR

A normal C function is not an interrupt handler. dcc and BDS C emit an ordinary
function prologue and `RET`; they do not install the interrupt vector, preserve
the complete interrupted register state, or emit `RETI`.

C can perform the handler's work when an assembly wrapper owns interrupt entry
and exit:

```asm
ISR:    PUSH    PSW
        PUSH    B
        PUSH    D
        PUSH    H
        CALL    CWORK          ; compiler/linker-specific external symbol
        POP     H
        POP     D
        POP     B
        POP     PSW
        EI
        DB      0EDH,04DH      ; RETI
```

The called C function must not use BDOS, block, or depend on nonatomic access
to data also modified by the main program. The exact external symbol spelling
and linkage depend on the selected CP/M C compiler, so inspect that compiler's
generated assembly or runtime conventions before linking the wrapper.

## Build and run the example

From CP/M, fetch and build `INTTEST` with the tools on drive B:

```cpm
B>ft -g inttest/inttest.sub
B:submit inttest
```

The submit file installs `INTTEST.COM` on drive A. Run it with:

```cpm
B>a:inttest
```

After approximately five seconds, the expected result is:

```text
Counting 50 interrupts at 10 Hz...
Interrupt count (hex): 0032
```