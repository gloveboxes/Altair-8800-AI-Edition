# Altair FT (File Transfer) App

A CP/M application that downloads files from a Remote FT Server.
`FT.ASM` is the maintained 8080 macro-assembler (MAC) implementation.
The legacy BDS C alternative has been removed.

## Building

MAC (macro assembler):

```
A> mac ft
A> load ft
```

`FT.SUB` builds the assembler version end-to-end (fetch, `mac`, `load`, copy
to `C:`, clean up) for the CP/M MCP build server.

## Usage

```
A> ft -g  <filename>    text file: normalize line endings to CP/M CR LF, add ^Z
A> ft -gb <filename>    binary file: store bytes verbatim, no ^Z
```

Use `-g` for source and text files (`.C`, `.ASM`, `.SUB`, `.H`, ...). It strips
any incoming CR and expands each LF to CR LF, so the saved file always has
canonical CP/M line endings whether the source used LF-only or CRLF, then
writes a single `^Z` end-of-text marker. This is what `SUBMIT`, `MAC`, and
`CC` expect.

Use `-gb` for binary files (`.COM`, images, archives, ...). Bytes are written
exactly as received with no `^Z`; the final partial 128-byte record is padded
with NUL (`00H`).

## Examples

```
A> ft -g test.txt
Downloading 'test.txt' from Remote FT Server...
Done (84 bytes)

A> ft -g subdir/hello.c
Downloading 'subdir/hello.c' from Remote FT Server...
Saving as 'hello.c'
Done (256 bytes)

A> ft -gb prog.com
Downloading 'prog.com' from Remote FT Server...
Done (3072 bytes)
```

## Protocol

The FT app uses dedicated I/O ports **21 (0x15)** and **22 (0x16)**:

- **Port 21 (OUT)**: Command port - sends commands to start transfer, request chunks, close
- **Port 21 (IN)**: Status port - returns IDLE=0, DATAREADY=1, EOF=2, BUSY=3, ERROR=255
- **Port 22 (OUT)**: Data port - sends filename characters, then NUL
- **Port 22 (IN)**: Data port - reads count byte (zero means 256), then exactly that many bytes

FT does **not** use shared-service length framing. Older FT binaries using
ports 60/61 cannot fetch their own replacement from the migrated firmware:
install the newly assembled `FT.COM` into the boot/tools images first using
the host-side `scripts/cpm-install.ps1` helper. The MCP pristine tools image
must contain the new client before any submit workflow can run.

## Requirements

- Altair emulator with WiFi and Remote FT support
- Remote FT Server running on the configured IP address:8090
