# Apple-1 emulator for CP/M

## Run the emulator in CP/M

With `A1.COM` on the current CP/M drive, enter this at the CP/M prompt to start directly in BASIC:

```text
A1 -a:E000
```

Enter a small BASIC program, then run it:

```basic
10 PRINT "HELLO"
20 END
RUN
```

To start in the Apple-1 Woz monitor instead, enter:

```text
A1
```

At the monitor, enter:

```text
E000R
```

This starts BASIC. The monitor also lets you inspect memory, enter machine code, and run programs.

### Why `-a:E000`?

The `-a:` option sets the emulated 6502's starting program counter. Addresses are hexadecimal:

- `$E000` is the entry point of the embedded Woz BASIC interpreter.
- `$FF00` is the entry point of the Woz monitor and is the default starting address.

`A1 -a:E000` enters BASIC directly. Running `A1` and then entering `E000R` at the monitor gets you to the same interpreter; `R` is the monitor's run command.

## Use the Woz monitor

The Woz monitor is a tiny interface for inspecting memory, changing bytes, and running 6502 machine-code programs. Start it by running `A1` without arguments.

All addresses and byte values are hexadecimal. Enter them without `$` or `0x`, and press Enter after each command.

| Command | Action |
| --- | --- |
| `0280` | Display the byte at address `$0280`. |
| `0280.028F` | Display memory from `$0280` through `$028F`. |
| `0280: A9 C1 20 EF FF` | Write these five bytes into memory starting at `$0280`. |
| `0280R` | Run machine code starting at `$0280`. |
| `E000R` | Start the embedded BASIC interpreter. |

For example, inspect the first 16 bytes of BASIC:

```text
E000.E00F
```

Or write three bytes into RAM and read them back:

```text
0280: 01 02 03
0280.0282
```

These byte-entry examples demonstrate editing memory; they are not complete executable programs.

After examining an address, entering just `R` runs from the monitor's current examined address. An explicit address such as `E000R` makes the intended entry point clear.

Press Escape to abandon the current input line and return to the monitor's `\` prompt. Backspace or Delete erases the previous input character using the Apple-1 underscore rubout convention.

The monitor has no assembler or disassembler: machine code is entered and inspected as raw hexadecimal bytes. It also has no built-in filesystem commands. A1 provides file loading through its command-line options and Ctrl+L, described below.

## Stop or reset the emulator

| Key | Action |
| --- | --- |
| Ctrl+C | Exit A1 and return to CP/M. |
| Ctrl+Q | Exit A1 and return to CP/M. |
| Ctrl+R | Soft reset to the Woz monitor. |
| Ctrl+L | Prompt for a text file to feed in as keyboard input. |

These keys are handled when the emulated program checks the keyboard. If it is stuck in a loop that never checks input, they may not respond. Ctrl+C and Ctrl+Q exit the emulator; they do not simply stop a BASIC program and leave you at the BASIC prompt.

## Load programs and keyboard input

To feed a text file into the emulator as though it were typed:

```text
A1 -a:E000 -l:HELLO.BAS
```

The file can contain numbered BASIC lines and a final `RUN` command. Files must be accessible from CP/M. Ctrl+L lets you select an input file while A1 is running. CP/M's Ctrl+Z end-of-file marker is recognised.

To load a 6502 machine-code program from an Apple-1 hex dump or Intel HEX file:

```text
A1 -a:0280 DEMO.HEX
```

Here, `$0280` is an example entry point; use the address required by your program. An Apple-1 format hex file can supply its first address as the default entry point. For Intel HEX files, specify the entry point with `-a:`.

### Command-line options

```text
A1 [options] [hexfile]
```

| Option | Meaning |
| --- | --- |
| `-a:addr` | Set the starting address in hexadecimal. Defaults to `$FF00`, or the first address supplied by an Apple-1 hex file. |
| `-l:file` | Read a file as keyboard input. |
| `-h` | Replace selected monitor routines with host-side hooks for output and exit handling. Requires a hex file. |
| `-x` | Exit when execution reaches the monitor's line-input entry at `$FF1F`. Useful for programs that return to the monitor. |
| `hexfile` | Load an Apple-1 or Intel HEX file into emulated memory before execution. |

## How `a1.c` works

`a1.c` implements an Apple-1 emulator for CP/M 2.2. It includes a BASIC interpreter as embedded 6502 machine code, together with the Woz monitor.

```text
Your BASIC program
        |
        v
Woz BASIC interpreter (embedded 6502 machine code)
        |
        v
6502 CPU emulator written in C
        |
        v
CP/M console and file services
```

### Emulated processor

The `MOS_6502` structure holds the accumulator (`a`), index registers (`x` and `y`), stack pointer (`sp`), program counter (`pc`), and processor status flags.

`emulate()` repeatedly fetches the opcode at `cpu.pc`, executes it using a large `switch`, updates the emulated state, and advances or redirects the program counter. For example, opcode `$E8` implements `INX`, incrementing X and updating the negative and zero flags.

Some hot paths use Z80 assembly when built with the supported CP/M compilers to improve execution speed.

### Memory layout

| Emulated address | Purpose | C storage |
| --- | --- | --- |
| `$0000–$3FFF` | First 16 KB of RAM, including zero page and the 6502 stack | `m_0000` |
| `$4000–$7FFF` | Additional 16 KB of RAM in the current `APPLE1_32K` configuration | `m_4000` |
| `$D010` | Keyboard character | `m_d000` and input handling |
| `$D011` | Keyboard status | `m_d000` and input handling |
| `$D012` | Display output | `m_d000` and output handling |
| `$E000–$EFFF` | 4 KB Woz BASIC image | `m_e000` |
| `$FF00–$FFFF` | 256-byte Woz monitor image | `m_ff00` |

`mem_base` and `get_mem()` translate emulated addresses into pointers to the corresponding C arrays.

The large hexadecimal byte array named `m_e000` **is the BASIC interpreter**. Its parsing, expression evaluation, and BASIC statement execution are implemented in those 6502 instructions. There are no equivalent C functions such as `parse_basic()` or `execute_print()` in this file.

### Keyboard and display

`m_load()` handles emulated keyboard reads using the CP/M console or a selected input file. It converts characters to uppercase and sets the high bit expected by the Apple-1 software. Backspace and Delete become the Apple-1 underscore rubout character.

`m_store()` handles writes to the display register at `$D012`, printing characters to the CP/M console and translating carriage returns into host newlines.

### Startup

`main()` parses the command-line arguments and clears the CPU structure. `invoke_command()` initialises the processor, optionally loads a hex file, sets the requested start address and stack pointer, installs any requested monitor hooks, and calls `emulate()`.

The default start address is `$FF00`; `-a:E000` changes it to BASIC's entry point.
