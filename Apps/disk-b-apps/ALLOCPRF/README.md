# ALLOCPRF: banked allocator performance

The build uses shared bank-memory and BIOS support from
[BANKSDK](../BANKSDK/README.md), not private copies in this example.

Copy `ALLOCPRF.COM` to a CP/M disk and run `ALLOCPRF`. It requires the matching
BIOS ABI 2.6, bank-1 allocator firmware, and millisecond stopwatch on port
`17h`. Stopwatch 0 is reserved while the benchmark runs.

The program benchmarks only the BIOS ABI 2.6 extent allocator. Each
measurement runs **512 allocate/free pairs**, reporting total milliseconds
and average microseconds per pair. Allocation, free, loop and success-check
overhead are included. This is not an isolated malloc latency. No payload
data is copied. Initialization, warm-up, allocation-size/free-space validation
and console output happen outside the timed interval. `<1 ms` means below
stopwatch resolution; a timer error fails the benchmark.

The request sizes are 256, 1,024 and 2,048 bytes. Cases use an empty
heap, a held 1 KiB prefix, and four small holes separated by live allocations.
Run the program several times to check repeatability.

The heap must be empty before starting. Existing allocations are preserved
and cause the benchmark to exit. The program frees its own allocations and
verifies that the original free space is restored; it does not reset the heap.

Build from this folder:

```sh
export DCC_DIR=/Users/dave/GitHub/dcc
"$DCC_DIR/dccmake"
cp build/ALLOCPRF.COM ALLOCPRF.COM
```

For a short emulator run, build from this folder with:

```sh
"$DCC_DIR/dccmake" dcc-define=ITERATIONS=4 dcc-build-dir=build/emulator
```

Then run from the repository root:

```sh
build/disk-emulator/bin/python src/common/tests/allocperf_z80_test.py
```

The emulator tests verify extent allocation, request sizes, scan positions,
heap cleanup, reporting, zero-resolution readings and timer errors. Their
injected timer readings do not measure hardware performance.
