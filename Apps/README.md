# Build Apps for CP/M drives A–D

Stop the emulator, then run from the repository root:

```powershell
pwsh ./Apps/build-all.ps1
```

The folder containing an application controls where it is installed:

| Folder | Destination image |
| --- | --- |
| `disk-a-apps` | A: `disks/cpm63k.dsk` |
| `disk-b-apps` | B: `disks/bdsc-v1.60.dsk` |
| `disk-c-apps` | C: `disks/escape-posix.dsk` |
| `disk-d-apps` | D: `disks/blank.dsk` |
| `disk-all-apps` | All four images above |

Applications can sit directly in a disk folder or in nested application
subfolders. Other folders, including `disk-ignore`, `DATE`, `COMMON`, `GAMES`
and `UTILITIES`, are not scanned for applications. Nested tests and fixtures
are excluded, as are generated sources and build directories.

- C applications are built with dcc using their `dccmake.txt`, preserving
  multi-source configurations and output names. ISAM builds ISAM.LIB in bank 1
  and the DOCTOR/DOCGEN clients in bank 0. In directories without a configuration, each `.c` file is treated as
  a standalone program using dccmake defaults.
- Fixed-address libraries use `dcc-output-format=lib` and the matching special
  `l80cc`, not the ordinary COM linker.
- A composite application has a root `dccmake.txt` containing
  `dcc-projects=lib,host`. It is discovered once; its children are not also
  scheduled as standalone applications. One root `dccmake` builds children
  in the declared order, generating the typed contract/wrappers before the
  host build. Both outputs are collected before either is installed.
  Standalone library jobs also precede standalone C hosts.
- `.asm` applications are assembled and linked with M80/L80 through `ntvcm`.
  Sources marked `; altair-z80 standalone` (including LS) use native
  `m80c`/`l80c` and supply their own startup and helpers.
  When C and assembly implementations produce the same executable, the C build
  takes precedence (currently MCPDONE and SHEETS).
- `.bas` applications are copied as source files with CRLF line endings and
  a CP/M Ctrl-Z end marker so MBASIC can load them. Repository sources retain
  their original line endings.

[BANKSDK](disk-b-apps/BANKSDK/README.md) contains shared banked-memory and
library support, not applications. The bulk builder excludes that folder
and its descendants; application configurations reference its sources.

Shared applications are built once and installed on all four drives. FT follows
its folder placement; the build-all script does not additionally install it on B:.
Files are installed in CP/M user area 0. Duplicate destination names are rejected
before any builds or disk writes. Installation deletes the existing destination
file before copying its replacement, freeing its disk blocks first. It replaces only the named file and
preserves unrelated files and system tracks. Earlier successful installations
remain if a later step fails; `BUILD ALL PASS` appears only after all steps succeed.

Preview the discovered applications and destinations without building or writing:

```powershell
./Apps/build-all.ps1 -ListOnly | Format-Table Kind, Output, Drives, Source -AutoSize
```

To assemble/install only the assembly applications (still excluding alternate
implementations whose executable names belong to C applications):

```powershell
./Apps/build-all.ps1 -AssemblyOnly
```

Requirements: PowerShell 7, the updated composite/library-capable `dccmake`,
`ntvcm`, `cpmrm` and `cpmcp` on PATH,
plus `M80.COM` and `L80.COM` in the dcc directory. Set `DCC_DIR` and
`NTVCM_DIR` as required by the C application configurations. Their shared source
and include paths point to `Apps/COMMON` in the reorganized layout.

Override tool locations or destination images when needed:

```powershell
./Apps/build-all.ps1 -DccDirectory /path/to/dcc -Ntvcm /path/to/ntvcm `
    -DiskA /tmp/a.dsk -DiskB /tmp/b.dsk -DiskC /tmp/c.dsk -DiskD /tmp/d.dsk
```

`-Dccmake` can separately select the C build executable. Image overrides must
refer to existing Altair disk images. Full images report an installation error;
the script does not remove unrelated files to make room.

For the copied LIBDEMO example, supply the special linker explicitly because
its original relative linker path refers to the source repository's layout:

```powershell
./Apps/build-all.ps1 -DccDirectory /path/to/dcc `
    -LibraryLinker /path/to/Z80ROMlessSBC/l80cc
```

`-LibraryLinker` defaults to `DCC_L80CC` when set. It overrides only LIB
linking; host COM programs retain their normal linker. Without an override,
the child configuration's linker setting is used. DCC_DIR is set for each
build from `-DccDirectory` (or inferred from dccmake if absent) and restored
afterward. No JSON manifest or Python library builder is involved.
Library intermediates are retained because they can contain generated
sources needed by a host.

Run the isolated build/discovery regression tests with:

```powershell
python3 -m unittest scripts.test_build_all -v
```

These compile real copied library/host sources in temporary directories,
test ordering and early failure, and use an installation recorder without
writing the repository's disk images.

Run the isolated disk installation regression check with:

```powershell
python3 ./scripts/test_cpm_install.py
```

This requires the built local emulator, PowerShell and cpmtools. It installs
into temporary images, checks track/sector headers and checksums, and boots
CP/M to run a program and switch between B: and A:. Unused sectors can retain
track-0 headers, so the installer initializes address fields as well as payloads
and checksums. The two boot tracks and trailing image bytes are preserved.
