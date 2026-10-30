# LIBDEMO: loadable C library and end-to-end client

This example builds **DATA.LIB**, a small bank-1 data service, and
**DATATEST.COM**, its bank-0 client and end-to-end test program.
It demonstrates values, copied buffers, flat structs, file I/O and bank-owned memory
without handwritten assembly, adapters or an interface file.

The [library source](lib/DATA.C) defines the API. DCC discovers its public,
non-`static` functions and generates the client header and wrappers.
Private helpers remain `static`; compiler/runtime helpers and global data
are not exports.

## What client code looks like

Include `API.H`, load the library, then call its functions normally:

```c
#include <stdio.h>

#include "API.H"
#include "BANKLIB.H"

int main(void)
{
    unsigned char text[] = "Bank 1";
    unsigned int result, ok, closed;
    if (!bank_library_open("DATA.LIB"))
    {
        printf("Load failed: %u\n", bank_library_last_error());
        return 1;
    }
    result = add16(120, 23);
    ok = result == 143 && bank_library_last_error() == BL_OK;
    if (ok)
        ok = upper(text, sizeof(text) - 1) == sizeof(text) - 1 &&
             bank_library_last_error() == BL_OK;
    if (ok)
        printf("%u: %s\n", result, text);  /* 143: BANK 1 */
    else
        printf("Call failed: %u\n", bank_library_last_error());
    closed = bank_library_close();
    if (!closed)
        printf("Unload failed: %u\n", bank_library_last_error());
    return ok && closed ? 0 : 1;
}
```

The generated wrappers handle name lookup, signature validation, argument
packing and buffer copying. No caller address is passed into bank 1.
The host still owns library lifetime and checks two kinds of result:

- `bank_library_last_error()` describes transport/ABI failures. `BL_OK` means the call ran,
  **not** that the requested operation succeeded.
- The ordinary C return value describes the operation. For example, zero
  from `blknew` means allocation failed or a block is already open.
  Zero from `add16(65535U, 1)` is a successful 16-bit wraparound result.

See [DATATEST.C](host/DATATEST.C) for the full runnable checks and
[HOSTCHK.C](host/HOSTCHK.C) for test-only setup and host-state checks.
Real clients can use `bank_library_open`/`bank_library_close` directly, as above.

## Small API

Every function returns `unsigned int`. Byte counts and offsets are explicit;
buffer calls accept at most 128 bytes.

| Call | Purpose and result |
| --- | --- |
| `add16(left, right)` | Add two 16-bit unsigned values, modulo 65536. Arguments are copied. |
| `upper(data, count)` | Convert exactly `count` ASCII bytes to uppercase and copy them back; return the count. The terminator need not be included. |
| `filerd(data, count)` | Open `A:AUTORUN.COM` in bank 1, read up to `count` bytes from its beginning, close it, and copy the bytes back; return the number read. Each call reopens the file. |
| `blknew(spec, info)` | Copy in a `BlockSpec`, allocate one 4096-byte block, initialize byte `i` to `(unsigned char)(spec->seed + i)`, and copy out a `BlockInfo`. Return its opaque handle. Only one block can be open. |
| `blkread(handle, offset, data, count)` | Read a bounded slice of that block into a host buffer; return the count. |
| `blkfree(info)` | Copy a `BlockInfo` in/out, free its block, clear handle/size/byte sum, set signed state to -1, and preserve its label; return 1. |

Other operation failures return zero. `filerd` also returns zero on an empty
file, open/read/close failure, or invalid/zero count; it is deliberately a
small example, not a general file-service API. Output contents are only valid
for the returned byte count. The generated `out` wrapper copies the requested
buffer length, so do not depend on unused bytes remaining unchanged.
An operation returning zero with `BL_OK` is not a transport rejection.

Two automatic lifecycle hooks complete the eight exports:
`onload(void)` reports bank-1 memory and returns 1;
`onunload(void)` frees any outstanding block and verifies that the DCC heap
has no committed allocations. Clients do not call the hooks themselves.

Handles are not pointers. The runtime supplies non-repeating 16-bit call
tokens, so freeing/replacing a block or reloading the library cannot revive
an old handle within the same host process. Exhaustion is an explicit
transport error; the last token is reserved for unload cleanup.
Handles are not persistent IDs or a security boundary.

### Declaring copied buffers

Counted byte buffers use:

```c
#pragma dcc bank_buffer upper 1 inout 2 128
#pragma dcc bank_buffer filerd 1 out 2 128
#pragma dcc bank_buffer blkread 3 out 4 128
```

For `upper`, parameter 1 is copied in and back out, parameter 2 supplies its
length, and the maximum is 128 bytes. For `blkread`, parameter 3 is an output
buffer and parameter 4 is its length. DCC checks the annotations against the
actual C parameter types. Directions `in`, `out` and `inout` are supported.
An oversized buffer or a nonzero count with a null pointer is rejected
before dispatch, without modifying caller data.

### Flat structs and the generated contract

The library declares these pointer-free records:

```c
struct BlockSpec {
    unsigned int seed;
    unsigned char label[8];
};
struct BlockInfo {
    unsigned int handle;
    unsigned int size;
    unsigned long byte_sum;
    signed char state;
    unsigned char label[8];
};

#pragma dcc bank_record blknew 1 in
#pragma dcc bank_record blknew 2 out
#pragma dcc bank_record blkfree 1 inout
```

`BlockSpec` is 10 bytes and `BlockInfo` is 17. The generated `API.H`
contains complete struct definitions, function declarations, annotations and
compile-time size/offset/integer-width checks. The host writes ordinary C:

```c
struct BlockSpec spec = {37, {'B', 'A', 'N', 'K', 'D', 'A', 'T', 'A'}};
struct BlockInfo info;
unsigned int handle = blknew(&spec, &info);
/* Check handle and bank_library_last_error() before using info. */
```

Successful creation returns size 4096, byte sum 522240, state 1 and the
unchanged eight-byte label. The byte sum is a 32-bit value. `blkfree(&info)`
demonstrates in/out copying, including the negative signed-byte state.
The block itself stays in bank 1; the record carries metadata, not a pointer.

Record pointers always name exactly one record and cannot be NULL. `const`
record pointers permit only `in`. OUT records start zero-filled, including
on a dispatched operation that returns zero; transport failures leave caller
records unchanged. Copied pointers do not preserve aliasing: overlapping
arguments involving a record are rejected if either is writable. Library code
must not retain its temporary, bank-local record pointer after returning.

Only named flat structs with unqualified 8/16/32-bit integer members and
fixed one-dimensional integer arrays are supported. Typedefs resolve through
the compiler; the generated public type uses the struct tag. Anonymous,
nested, pointer-bearing, union, bitfield and by-value aggregate interfaces
are rejected, as are const/volatile members.

Records use constant-offset bulk copies, not per-field serialization or
allocation. `blknew` needs a 29-byte body and `blkfree` 19; their generated
host arrays use those exact sizes. Fixed-size adapters check the body length
once before accessing data. Canonical signatures contain the complete field
names/types/order/array bounds/offsets and size, so same-size incompatible
records are rejected. The existing 256-byte frame and 255-character signature
limits remain in force. Rebuild the library and client together; the record
support does not require a new firmware ABI.

Calls are synchronous and non-reentrant. Frames have at most 256 body bytes;
open reserves one 512-byte transfer extent and close releases it. Calls reuse
that frame without allocating/freeing. Generated wrappers cache each full
signature binding per module generation; raw calls still validate every time.
Generation/epoch checks reject stale ownership before writes. DMA/search state
is isolated between banks, while current drive and user remain shared.
Pointer graphs, callbacks, unions, by-value structs and variadic cross-bank arguments
are unsupported and rejected rather than guessed. DCC's `meminfo_t` is used
locally inside each bank, not passed across the boundary.

## Run the complete example

Install the fresh **DATA.LIB** and **DATATEST.COM** on the same CP/M drive.
The file-read check also requires the normal boot file **A:AUTORUN.COM**,
with at least 128 bytes. Then run:

```text
B>datatest
DATATEST: ordinary C calls, separate banks.
[1/8] Load DATA.LIB and run onload
...
[8/8] Reload, reject old handles and clean up an open block
PASS: reload isolation and unload cleanup.
PASS: all 8 bank-1 library checks.
```

Each numbered section prints before doing its work and has its own PASS.
Failures identify the section and current transport error, then attempt
cleanup. The program exits nonzero on failure.

| Step | What is verified |
| --- | --- |
| 1. Load | Checksummed library load, lifecycle hook and memory report. |
| 2. Values | Addition, unchanged caller values and unsigned wraparound. |
| 3. Text | In/out copying, untouched bytes beyond the count, the 128-byte limit, oversized and null-buffer rejection. |
| 4. File | A 128-byte bank-1 file read, byte sum, reopen-at-start behavior and output-buffer guards. The emulator checks the sum against the actual boot-file fixture. |
| 5. ABI | All eight names/signatures; wrong signature (including a changed record field name), truncated scalar/record frames and oversized-frame rejection with unchanged caller data. This section alone uses low-level `bank_library_call` to test intentionally invalid calls. |
| 6. Records and blocks | Record IN/OUT/INOUT copying, unchanged input, output zeroing, 32-bit values, signed state, fixed arrays, guards, NULL/alias rejection, correct chunk contents, one-live-block rule, transfer/object bounds, zero/stale handles, replacement handles and double-free rejection. |
| 7. Unload | Freeing a still-open block, an empty DCC heap, directory-search preservation and full recovery of the shared transfer pool. |
| 8. Reload | Old handles remain invalid, the final block byte is correct, and unloading frees a still-open block and leaves the DCC heap empty. |

This is a full regression example, not a timing benchmark or quick two-call
demo. It loads the library twice and creates/initializes three 4 KiB blocks.
Every dispatched call performs validation and cross-bank transfer/cleanup.
The progress headings explain that work; no performance improvement is implied.

## Build and generate the contract

```text
LIBDEMO/
  dccmake.txt             # dcc-projects=lib,host
  lib/
    DATA.C, dccmake.txt
    build/DATA.LIB
  host/
    DATATEST.C, HOSTCHK.C, HOSTCHK.H, dccmake.txt
    generated/API.H, generated/B1HOST.C
    build/DATATEST.COM
```

Use the matching updated DCC compiler, `dccmake` and runtime with `meminfo()`.
Both COM and LIB builds use DCC's native `l80c`. Rebuild it if necessary:

```sh
export DCC_DIR=/path/to/dcc
cc -std=c89 -O2 -Wall -Wextra -Werror "$DCC_DIR/src/l80c/l80c.c" -o "$DCC_DIR/l80c"
(cd examples/LIBDEMO && "$DCC_DIR/dccmake")
```

One root build generates the API from the library, builds the library, then
builds the host. A failed child stops the unit. Both children use the same
DCC linker; LIB children select its `--lib` output mode.
Override the LIB linker with `dcc-library-linker=/path/to/l80c` if needed.

To obtain the client contract **before writing the client**:

```sh
(cd examples/LIBDEMO && "$DCC_DIR/dccmake" dcc-contract-only=true)
"$DCC_DIR/dccmake" -help
```

Contract-only mode compiles the library for metadata and emits
`host/generated/API.H` and `host/generated/B1HOST.C`; it skips COM children
and does not assemble, link or install anything. It needs no assembler, linker
or runtime file. Normal builds refresh the same generated contract.

The library configuration selects:

```ini
dcc-exports=auto
dcc-library-api=auto
dcc-library-host-dir=../host/generated
```

The client includes `API.H` from `host/generated/` and compiles
`generated/B1HOST.C` with BANKLIB/BIOSBANK from the shared
[BANKSDK](../BANKSDK/README.md) folder. Both configurations put shared
headers on the compiler's include path; the host also adds `generated`.
Source files include header names, not relative paths into sibling projects.
Do not edit
generated files or include the generated header from the library itself:
generation must work from scratch. A handwritten contract is still supported
by setting `dcc-library-api` to a header path instead of `auto`.
Public names must fit eight characters and must not collide in their first
five characters, because of M80's six-character C-symbol limit.

Fresh outputs are [DATA.LIB](lib/build/DATA.LIB) and
[DATATEST.COM](host/build/DATATEST.COM). The bundled reference
[library](lib/DATA.LIB) and [symbol map](lib/DATA.SYM) are refreshed after tests.
There is no custom API generator, export list or separate test executable.

## Memory and firmware compatibility

Both memory reports use DCC's `meminfo()` from `<stdlib.h>`.
Capacity is heap limit minus start, committed is break minus start, and
growth is limit minus break. Committed includes allocator metadata and
reusable interior free blocks; growth is not total free payload.
Stack is the configured reserve, not measured current usage.

Bank 1's heap starts after the linked image/BSS and ends below the configured
stack. A 512-byte stack ends it at `C600h`; code/data changes automatically
change its capacity. The separate transfer pool is 5632 bytes at `C800h`.

Typed libraries require the version-4 loader and host runtime (BIOS ABI 2.6,
allocator revision 6). The updated loader also accepts legacy version 3;
the typed host deliberately requires version 4. Older firmware needs a loader
update, not just new COM/LIB files. Follow the
[firmware build and flashing workflow](../../docs/docs/en/system/firmware-build.md).
Building this example does not install disks or flash hardware.
