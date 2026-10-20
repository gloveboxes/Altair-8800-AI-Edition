# Build All dcc and Assembly Apps

`build-all.ps1` builds the host-compiled dcc applications and maintained `.ASM`
applications, installing each generated `.COM` file into the default CP/M C:
disk image (`disks/escape-posix.dsk`). FT is also installed on the B: tools
disk (`disks/bdsc-v1.60.dsk`) so `B:FT` cannot remain on the old I/O ports.

Stop the local emulator before updating its disk image, then run:

```powershell
pwsh ./Apps/BUILDALL/build-all.ps1
```

The script first runs the existing `build-app.ps1` wrappers in app-name order.
It then discovers `.ASM` files directly inside immediate `Apps` subdirectories
and builds them in path order using `scripts/build-asm-app.ps1`.

An existing app wrapper takes precedence over an assembly source with the
same name as its directory: DATE, SHEETS and MCPDONE keep their C builds and
do not also assemble their alternate `.ASM` implementations. Distinct assembly
companions, such as `ATTNINF.ASM`, are included.

The current assembly cohort is ATTNINF, AUTORUN, EDIT, ENV, ESP32, FT, INTTEST,
SLEEP and VIBE. BASIC files and standalone `.MAC` variants are not included.
Legacy BDS C applications and their supporting SDK/compiler archives remain
retired. TICTAC is maintained as a dcc port and is included through its C
build wrapper. Current service-using C apps share `Apps/COMMON/DXSVC.C`.

To build/install only the assembly cohort without rebuilding C applications:

```powershell
pwsh ./Apps/BUILDALL/build-all.ps1 -AssemblyOnly
```

The standard application disk has about 298 KB of usable space. The full C
cohort includes the sizeable DOCTOR and DOCGEN tools, so it may leave too little
room for the assembly apps and existing data. The script never automatically
deletes unrelated files to make space. Free space explicitly or use a different
image with the individual assembly helper; a full build will reinstall the DOC
tools, whereas `-AssemblyOnly` will not.

## Assembly requirements

- `ntvcm` on `PATH`.
- `M80.COM` and `L80.COM` from the dcc compiler directory. By default that
  directory is located through `dccmake` on `PATH`.
- The existing `cpmtools` requirements for disk installation.

Override the assembly tool locations if needed:

```powershell
pwsh ./Apps/BUILDALL/build-all.ps1 -DccDirectory /path/to/dcc -Ntvcm /path/to/ntvcm
```

Assembly runs in a fresh temporary directory, using CRLF source with M80
absolute mode so the existing `ORG 100H` entry addresses are preserved.
The helper checks assembler/linker output and installs through the same
`scripts/cpm-install.ps1` used by dcc apps. Generated `.COM` files are also
copied beside their sources; temporary tools and intermediate files are removed.
No existing CP/M FT binary, remote server or HTTP connection is used.

To build and install just one assembly application:

```powershell
pwsh ./scripts/build-asm-app.ps1 -Source ./Apps/FT/FT.ASM
```

The helper accepts `-DiskImage` to override C: and, for FT, `-ToolsDiskImage`
to override B:. Stop the emulator before writing either image. Installation
replaces only the named `.COM`, preserving other files and system tracks.
The build stops on the first error and prints `BUILD ALL PASS` only after
every compilation and disk installation succeeds.

## Regression test

With Node.js, PowerShell and the assembly/disk tools installed:

```sh
node Apps/BUILDALL/tests/asm_build_regression.mjs /tmp/altair-asm-tests /path/to/dcc /path/to/ntvcm
```

This builds all nine assembly apps against isolated copies of the disk images,
checks installed bytes (including both FT copies), verifies the `0100H` entry
layout and proves that assembler errors leave the previous executable and
disk images unchanged. It does not modify the repository's disk images.
