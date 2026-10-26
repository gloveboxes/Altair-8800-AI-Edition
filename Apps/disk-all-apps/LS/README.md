# LS

CP/M 2.2 directory listing app, implemented in self-contained Z80 assembly in
`LS.ASM`. It has its own startup, 512-byte stack and BDOS/memory helpers.
The build uses native `m80c`/`l80c` without linking the DCC runtime.

On the bundled CP/M 2.2 system, LS preserves the CCP's entry stack and returns
directly to its caller after restoring the original drive and default DMA.
Normal completion does not invoke BDOS function 0 or the BIOS warm boot.
This requires a resident CCP that calls transient programs with a valid
return address, as the bundled CCP does; LS keeps its code and private stack
below that resident CCP.

The app scans raw CP/M directory entries so all extents are included, sorts
8.3 names, reports allocation-block sizes, supports CP/M filespec filtering
and temporary drive selection, and can sort largest files first with `-S`.
Files with equal sizes remain alphabetical. The original drive is restored
before returning to the CCP. The column heading is bright blue on VT100
compatible terminals and resets to the terminal default before file output.

```text
LS
LS -H
LS *.COM
LS A:
LS A:*.COM
LS -S
LS -S *.COM
LS -S A:*.COM
LS *.COM -S
LS A:*.COM -S
```

`-H` (or `-h`) displays help and is used alone. `-S` may appear once before
or after a single filespec. Filespecs use an optional drive letter A–P,
an 8-character name and a 3-character extension. `?` matches one character;
`*` fills the remainder of a name or extension and must end that field.
Unknown or repeated options, extra filespecs and malformed arguments produce
`LS: invalid argument: <argument>` followed by a help hint. Help and argument
errors return directly to the CCP without a directory scan or warm boot.

Before selecting an explicit drive through BDOS, LS calls BIOS `SELDSK` and
rejects an unsupported drive (`HL=0`). It restores the original BIOS drive
after the probe, keeping BIOS and BDOS selection state consistent. This
checks BIOS support for the drive letter; it does not test disk readability.
Unavailable or out-of-range drives report `LS: invalid drive: <filespec>`.

## Build

With `DCC_DIR` and `NTVCM_DIR` set to the DCC and NTVCM checkouts, run the
repository build from `Apps` to build and install LS on all four disk images:

```sh
pwsh ./build-all.ps1
```

To build and install LS on the default C: image only, run:

```sh
pwsh ./build-app.ps1
```

Verify listing behavior and the absence of warm boot on temporary disk images:

```sh
python3 ../../../scripts/test_ls_return.py
```

The check instruments the BIOS warm-boot entry, verifies a deliberate warm
boot is counted, then checks repeated LS commands leave the counter at zero.
It also checks sorting, wildcard filters, empty results, drive restoration,
and BIOS drive validation (including a BIOS hook that makes D: unavailable).
