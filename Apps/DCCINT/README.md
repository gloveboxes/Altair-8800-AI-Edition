# DCC mixed C and Z80 interrupt example

`DCCINT.C` demonstrates a dcc program whose interrupt service path crosses
both languages:

1. C initializes state and calls the assembly installation routine.
2. Assembly saves the original IM 1 vector at `0038H`, installs a jump to the
   ISR, binds the C counter and handler addresses passed through the dcc ABI,
   and enables the timer through port 52.
3. The ISR preserves the complete primary and alternate Z80 register state,
   then calls the ordinary C function `irq_tick()`.
4. The C handler increments `irq_ticks` in memory.
5. Assembly executes `EI` and `RETI` after restoring the interrupted context.
6. C waits for 50 interrupts, then assembly disables the timer and restores
   CP/M's original vector.

The C handler deliberately does not call `printf`, BDOS, allocate memory, or
perform other runtime work. An interrupt may arrive while CP/M or the dcc
runtime is already active, so those services are not reentrant.

## Build

Set the dcc checkout location and run the build script:

```powershell
$env:DCC_DIR = "$HOME/GitHub/dcc"
pwsh ./build-app.ps1
```

This produces `DCCINT.COM` and installs it into the default C: disk image. The
source uses dcc's supported `#asm` block, so the regular `dccmake` pipeline
compiles, assembles, and links both languages as one module.

The example is verified on both the ESP32 Altair and the desktop
`altair-local` emulator, where I/O port 52 provides the periodic interrupt
source. Generic ntvcm builds without the port 52 device cannot drive it.
