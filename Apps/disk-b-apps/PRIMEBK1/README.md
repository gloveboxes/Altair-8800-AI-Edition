# BANKCODE: direct entry into two bank-1 functions

Shared banked-memory and BIOS support lives in [BANKSDK](../BANKSDK/README.md).
For compiler-generated typed library calls, its [BANKLIB.H](../BANKSDK/BANKLIB.H)
and [BANKLIB.C](../BANKSDK/BANKLIB.C) provide signature-checked dispatch and bounded
cross-bank value transfers. See [LIBDEMO](../LIBDEMO/README.md) for automatic
exports, runtime enumeration, and bank-owned objects accessed through handles.

This DCC application allocates one bank-1 code block, two 512-byte stacks and
two independent context blocks. The code block contains two entry points:
offset 0 counts primes through an 8-bit limit; a second offset finds the next
prime after an 8-bit input. The bank-0 application passes the offset to the
new BIOS HEAP operation 6. Both functions use the same primality helper and
keep their mutable state in their own context block.

`bmcall(code, stack, entry_offset, context)` sets HL to the live code handle,
BC to a distinct live stack handle, IX to a 16-bit byte offset, and IY to the
bank-1 context pointer. Operation 6 checks that the offset is strictly within
the rounded code allocation, then enters `code + entry_offset`. It puts the
context pointer on the selected bank-1 stack as the function's first 16-bit
argument. A normal DCC C function sees it at `IX+4` after its usual `PUSH IX;
LD IX,0; ADD IX,SP` prologue. The function returns a 16-bit result in HL and
finishes with `RET`. The BIOS restores the caller's IX, IY, SP, bank and
interrupt state. This is a calling convention demonstration; the two bank-1
functions are still embedded assembly, not compiler-generated C.

The first context byte is the input. The second holds the count or next prime;
the third holds the last prime for the counting function. A zero next-prime
result means no larger prime fits in one byte. The program exercises both
entry offsets with inputs 30 and 50, tests the no-next-prime case after 254,
checks context isolation and stack use, then frees all allocations.

The example patches the two absolute `CALL` operands to their relocated
bank-1 helper address before copying the code. A future DCC bank-1 linker
must provide relocation records for calls and data references, and exported
function offsets instead of requiring this manual patching. The BIOS checks
the allocation boundary; the loader or caller must ensure each offset names
an instruction boundary and that all code and data fit in the allocation.

Bank-1 code must not call bank-0 code or BDOS, re-enter the allocator, or
enable interrupts. NMI is unsupported. Calls through the BIOS gateway are
sequential and require enough allocated stack for the function.

Build with the DCC toolchain:

```sh
cd examples/BANKCODE
DCC_DIR=/path/to/dcc /path/to/dcc/dccmake
```

Copy `BANKCODE.COM` to a CP/M drive. This example requires BIOS ABI 2.6 and
bank-1 allocator revision 6, checked through the public capability query, so update both the
CP/M boot package and Phase 10 firmware before running it.

The Z80 emulator regression runs the compiled COM with CP/M and the resident
banking code:

```sh
PYTHONPATH=src/common/tests build/disk-emulator/bin/python -m unittest \
  banked_heap_z80_test.BankedHeapExecution.test_compiled_direct_offset_primes
```

Expected output ends with
`PASS: direct offsets entered two bank-1 functions with DCC stack arguments.`
