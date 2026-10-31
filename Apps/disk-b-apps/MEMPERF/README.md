# MEMPERF: banked SRAM copy benchmark

Shared bank-memory and BIOS support comes from [BANKSDK](../BANKSDK/README.md),
not private copies in this example.

Build from this folder with the sibling DCC toolchain:

```sh
DCC_DIR=/path/to/dcc /path/to/dcc/dccmake
```

Copy `build/MEMPERF.COM` to a CP/M disk. BIOS ABI 1.0 and its matching
bank-1 firmware/CP/M image are required. The program uses stopwatch 0 on
port `17h` and frees its own banked allocation before exit.

Each timed direction repeats a 4,096-byte workload **32 times**, moving
128 KiB without enlarging the 4 KiB buffer or bank allocation. Per repeat,
the bulk case uses one 4 KiB BANKMEM API call and two 2 KiB local `memcpy`
calls. The chunk case uses 16 calls of 256 bytes in either memory type.
Preparation, poisoning, verification and printing are outside the timed
intervals. Both directions are verified afterward. Reported milliseconds
are the **total for all 32 repeats**, and KiB/s uses the full 128 KiB.
The 5,632-byte general pool must have a contiguous 4 KiB extent available.

A single 4 KiB copy measured with whole milliseconds can only report
800.0 KiB/s at 5 ms or 666.6 KiB/s at 6 ms. That is too coarse to diagnose
small changes. Repetition amortizes this rounding without subtracting
estimated overhead or bypassing the checked BANKMEM API. Loop, C-call and
live allocation-validation costs remain part of the measurement.

`bmalloc`, `bfree` and `bsize` execute in bank 1; transfers use the mirrored
READ/WRITE gateway. See [BANKED/BIOS-ABI.md](../BANKED/BIOS-ABI.md) for the
current ABI and [ALLOCPRF](../ALLOCPRF/README.md) for allocator timing.

## Reproducible CPU-cycle measurements

The independent Z80 emulator measures the actual linked checked C entry points,
not a replacement copy routine. Initialization, allocation and preparation
are excluded; every measured call still queries the live allocation size.
From the repository root, after building MEMPERF:

```sh
PYTHONPATH=src/common/tests build/disk-emulator/bin/python \
  src/common/tests/banked_sdk_benchmark.py
PYTHONPATH=src/common/tests build/disk-emulator/bin/python \
  src/common/tests/banked_transfer_benchmark.py
PYTHONPATH=src/common/tests build/disk-emulator/bin/python \
  src/common/tests/banked_sdk_benchmark.py --services
```

For 4 KiB total, with maskable interrupts disabled:

| Checked workload | T-states | Bank selects |
| --- | ---: | ---: |
| One 4 KiB read | 94,890 | 514 |
| One 4 KiB write | 94,870 | 514 |
| Sixteen 256-byte reads | 129,360 | 544 |
| Sixteen 256-byte writes | 129,040 | 544 |
| 256 16-byte reads | 695,296 | 1,024 |
| 256 16-byte writes | 690,176 | 1,024 |
| 4,096 one-byte reads | 14,819,328 | 16,384 |
| 4,096 one-byte writes | 18,857,984 | 24,576 |

One-byte writes require a read/modify/write burst to preserve neighbours;
batching adjacent operations is substantially cheaper when the application
can do so.

The `--services` benchmark measures the resident gateway independently of C
wrappers, with disabled interrupts:

| Resident operation | T-states |
| --- | ---: |
| Allocate 256 bytes | 1,559 |
| Query live pool allocation size | 554 |
| Query available bytes | 369 |
| Trivial banked function call (E=5) | 2,855 |
| Free that allocation | 1,776 |

These are microbenchmarks, not predictions for arbitrary allocation patterns
or library functions doing substantial work. The free list is
address-ordered first fit. Tests check every page-aligned handle,
interior/stale handles, coalescing, packed context restoration and
interleaved cross-drive searches.

These are CPU-cycle results, **not measured Pico throughput**: PIO bank-select
waits, other hardware waits and timer-port service time are not modeled.
Regression tests cover both interrupt states and enforce separate raw/checked
cycle budgets, data integrity, register/stack preservation and bank-switch counts.
An end-to-end MEMPERF test also checks the 128 KiB rate calculation, eight timed
directions, no output during timing and complete allocation recovery.
