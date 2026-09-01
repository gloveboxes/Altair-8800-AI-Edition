# Build and Install CP/M Apps

Host-compiled dcc applications can be built and installed directly into a
CP/M disk image. The ATTN application provides the reference workflow:

```powershell
pwsh ./Apps/ATTN/build-app.ps1
```

The script is cross-platform and runs under PowerShell 7 on macOS, Linux, and
Windows.

## Prerequisites

Install these host tools and make sure they are available on `PATH`:

- [PowerShell 7](https://learn.microsoft.com/powershell/scripting/install/installing-powershell)
  as `pwsh`.
- The dcc toolchain, including `dccmake`.
- [cpmtools](https://www.moria.de/~michael/cpmtools/), including `cpmrm` and
  `cpmcp`.

On macOS with Homebrew:

```bash
brew install cpmtools
```

On Debian and Ubuntu:

```bash
sudo apt install cpmtools
```

On Windows, install a Windows build of cpmtools and add the directory
containing `cpmrm.exe` and `cpmcp.exe` to `PATH`.

Verify the required commands from PowerShell:

```powershell
Get-Command pwsh
Get-Command dccmake
Get-Command cpmrm
Get-Command cpmcp
```

!!! warning "Stop the emulator first"
    Do not update a disk image while `altair-local`, the ESP32 emulator, or
    another process has that image mounted. Stop the emulator before running
    an app build script, then restart it after installation.

## Build and install flow

Each app's `build-app.ps1` delegates to `scripts/build-dcc-app.ps1`, which
performs these steps:

1. Reads the last active `dcc-output` assignment from `dccmake.txt` and appends
   `.COM`. For `dcc-output=ATTNC11`, the expected file is `ATTNC11.COM`.
2. Deletes the previous host output and temporary `build` directory. This
   prevents a failed build from installing a stale binary.
3. Runs `dccmake` and checks its native exit code.
4. Moves only the expected file from `build` into the application directory.
   A missing output is a terminating error.
5. Calls `scripts/cpm-install.ps1` with the disk image, host file, and CP/M
   destination such as `0:ATTNC11.COM`.
6. Deletes the old CP/M file with `cpmrm`, copies the new file with `cpmcp`,
   rebuilds the physical sectors, and atomically replaces the disk image.

PowerShell stops at the first failed command, so the disk installation only
runs after a successful compile, assemble, and link.

## Reusing the installer

Other application build scripts can call the shared installer directly:

```powershell
& ./scripts/cpm-install.ps1 `
    -Image ./disks/escape-posix.dsk `
    -HostFile ./Apps/FOO/FOO.COM `
    -CpmFile 0:FOO.COM
```

The CP/M destination starts with a user number, not a drive letter. The disk
image passed to `-Image` determines the emulated drive. In the default local
CP/M 2.2 configuration, `disks/escape-posix.dsk` is drive C:.

## Why the shared installer is required

The tracked `.dsk` files are raw MITS Altair 88-DCDD images, not flat arrays
of 128-byte CP/M records. Each image contains 77 tracks, 32 sectors per track,
and 137 bytes per physical sector, for a total of 337,568 bytes.

The Burcon CP/M 2.2 BIOS uses two physical sector layouts:

| Tracks | CP/M payload | Checksum | Trailer |
| --- | --- | --- | --- |
| 0-5 | Bytes 3-130 | Byte 132, sum of payload | Byte 131 is `0xFF` |
| 6-76 | Bytes 7-134 | Byte 4, sum of bytes 2-134 excluding byte 4 | Bytes 135-136 are `0xFF`, `0x00` |

Tracks 6-76 also apply a second physical-sector mapping after the CP/M skew
table:

$$
\text{physical sector} = (17 \times \text{translated sector}) \bmod 32
$$

The shared installer therefore:

1. Extracts each 128-byte payload into a temporary packed image while applying
   the track-dependent sector mapping.
2. Runs cpmtools against that packed image using the repository-level
   `diskdefs` entry named `altair88`.
3. Merges the modified payloads back into their physical sectors.
4. Regenerates the checksum and stop bytes required by the Burcon BIOS.

The central `altair88` definition records the CP/M disk parameter block and
skew table. It also sets `logicalextents 1`, matching the BIOS `EXM=0` layout.
Without that setting, files larger than 16 KiB receive incompatible directory
extent numbers and may appear in cpmtools but not in CP/M.

Do not run `cpmcp` directly against the raw 137-byte image. It does not account
for both Burcon sector layouts, the second track-dependent skew, or the physical
checksums.

## Validate an installation

Start the local emulator after the build:

```bash
./altair_local/build/altair-local
```

Then select the target drive, list the installed file, and run its help command:

```cpm
A>C:
C>DIR ATTNC11.COM
C>ATTNC11 -H
```

A `Bdos Err On C: Bad Sector` message indicates invalid physical framing,
checksum data, or sector mapping. Stop the emulator and restore the image from
version control before investigating further:

```bash
git restore disks/escape-posix.dsk
```

The authoritative BIOS implementation and disk parameter block are in
`disks/cpm64_bios.asm`.
