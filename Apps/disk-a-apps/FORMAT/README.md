# FORMAT - recovered CP/M disk initializer

`FORMAT.ASM` reconstructs the supplied 1,792-byte FORMAT.COM as symbolic Intel
8080 assembly, with BIOS-call compatibility fixes. The original author's
identity and source licence were not present in that executable; this recovery
does not claim authorship of the original program.

Original executable SHA-256:
`29b8fd664d52b5b5ea687db31456032b08f253dfde50e6b0a6122e21f38646fb`

## Fixes

- Set C=1 before BIOS WRITE. This requests a synchronous directory-type write;
  every destructive initialization write is committed before proceeding. The
  old program passed the DMA buffer's low address byte, 0xCA, as the write type.
  The SBC consequently rejected disk command 0xCC before writing a sector.
- Set B=0 before every BIOS SETTRK and SETSEC call, passing a complete BC value
  rather than leaving the previous DMA address in B.
- Set E=0 before SELDSK, indicating a first selection/login.
- Relocate both 128-byte DMA buffers and operation variables symbolically.
  BIOS jump-stub operands use labels too, so edits do not leave stale addresses.

The original command interface and messages are retained. `BLOCKS PER DISK`
displays DSM (the highest block index), as the original did. Commands use their
first uppercase letter: DISK, FULL, PARTIAL, TEST, STOP. PARTIAL was unimplemented
in the original and remains a no-op. This is a recovered utility, not a new
low-level physical floppy formatter.

## Use

Run FORMAT and select the intended drive. FULL writes 0xE5 to **every track,
including reserved system tracks**, then reads every sector and compares it.
It erases all files and any boot/system records on the selected drive. Do not
FULL-initialize the running system's boot drive: CP/M needs its system tracks
for subsequent warm boots. To initialize data drive D, select D, enter FULL,
and press C at the confirmation. A aborts. TEST reads without writing; STOP
returns to CP/M.

This program obtains geometry and sector translation from the selected BIOS.
It retains the original 8-bit track count/track and sector-number limits and
128-byte DMA records. Verification covers the supplied 77-track Altair-style
DPB and 128-track SBC DPB, both with 32 sectors per track and 2 KiB blocks.
The 77-track test runs that DPB through the SBC BIOS, not a physical Altair
controller. No real disks are formatted by the tests.

## Build

On CP/M, using the same tools as neighboring disk-A apps:

```
B:MAC FORMAT
B:LOAD FORMAT
```

`FORMAT.SUB` fetches the source through FT, builds it, and copies FORMAT.COM to A.
Review that destination before using it. The assembly is stored with CRLF line
endings for CP/M MAC. The compiled program is 2,013 bytes (one 2 KiB allocation
block). A CP/M LOAD-generated COM may include zero padding to a 128-byte record.

On the host, install z80asm and run:

```
python3 build-format.py
```

The helper translates the Intel mnemonic subset in this source to temporary
Z80 assembler syntax. All program instructions remain 8080-compatible.

## Tests

Using a Z80ROMlessSBC checkout with its CP/M images generated, cpmtools, z80asm,
and a Python environment containing the `z80` emulator package:

```
/path/to/Z80ROMlessSBC/build/disk-emulator/bin/python test-format.py \
  --repo /path/to/Z80ROMlessSBC
```

The tests run the real utility via CP/M and the BIOS. They verify full
write/readback and read-only scans for both geometries, exact sector coverage,
write type 1, preservation of other drives, abort, an injected read failure,
and independent compilation with the real CP/M MAC and LOAD. The initialized
binary bytes match the host build, with only possible trailing zero padding.
