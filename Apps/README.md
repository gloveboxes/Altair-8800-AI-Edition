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

Applications can sit directly in a disk folder or in an immediate application
subfolder. Other folders, including `disk-ignore`, `DATE`, `COMMON`, `GAMES`
and `UTILITIES`, are not scanned for applications. Nested tests and fixtures
are excluded.

- C applications are built with dcc using their `dccmake.txt`, preserving
  multi-source configurations and output names. ISAM builds both DOCTOR and
  DOCGEN. In directories without a configuration, each `.c` file is treated as
  a standalone program using dccmake defaults.
- `.asm` applications are assembled and linked with M80/L80 through `ntvcm`.
  When C and assembly implementations produce the same executable, the C build
  takes precedence (currently MCPDONE and SHEETS).
- `.bas` applications are copied as source files without compilation.

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

Requirements: PowerShell 7, `dccmake`, `ntvcm`, `cpmrm` and `cpmcp` on PATH,
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

Run the isolated orchestration regression checks with:

```powershell
pwsh ./Apps/BUILDALL/tests/build_all_regression.ps1
```
