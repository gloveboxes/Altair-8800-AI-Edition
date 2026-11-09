# Altair FT (File Transfer) App

A CP/M application that downloads files from a Remote FT Server.
`FT.ASM` is the maintained 8080 macro-assembler (MAC) implementation.

## Building

From the host, stop Altair Local and run:

```powershell
pwsh ./scripts/build-asm-app.ps1 -Source ./Apps/disk-c-apps/FT/FT.ASM
```

This uses the dcc compiler's `M80.COM` and `L80.COM` under `ntvcm` and
installs fresh `FT.COM` copies on the B: tools and C: application images,
including the MCP reset tools image.
The [build-all workflow](../BUILDALL/README.md) includes this step automatically.
It does not depend on an already working CP/M FT client.

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

FT uses ROMLESS-SVC/1 group **0x70** on shared ports **2/3/4**.
Write the group to port 2, then a 16-bit little-endian length and payload
to port 3. Poll port 2: 1=COMPLETE (ACK with OUT 2,0), 2=RESPONSE_READY,
4=BUSY, 255=ERROR. Responses and errors on port 4 have a 16-bit
little-endian length prefix.

| Payload | Result |
| --- | --- |
| `[1, filename bytes...]` (no NUL) | Open/replace; COMPLETE |
| `[3]` | `[state, up to 256 file bytes...]`, state 0=more, 2=EOF |
| `[4]` | Cancel/close; COMPLETE |

Reading the complete response returns the shared channel to READY.
EOF can accompany the final data bytes. Error 0x20 reports a missing/empty
`HTTP_BASE_URL`; error 0x21 reports a failed transfer. FT bounds status polling
and resets the channel before closing on failure. ESP32, Local and Docker
accept filenames of 1-127 bytes; a full read response has length 257.
Other shared services may run between reads without closing the file.
Ports `0x15/0x16` are available.

## Transfer backends

- ESP32 uses the configured remote FT server over TCP port 8090.
- Altair Local and Docker read the local Apps tree selected by `--apps-root`
  or `ALTAIR_APPS_ROOT`. The DCC adapter uses the session's native files root.
- Z80ROMlessSBC Phase 10 and its curl-enabled native adapter use HTTP(S).
  Relative paths use `HTTP_BASE_URL` in ENV; absolute HTTP(S) URLs do not
  require it. HTTPS requires a trusted certificate chain and hostname.

The CP/M port protocol is identical for these backends.
