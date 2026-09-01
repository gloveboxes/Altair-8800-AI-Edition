# Build All dcc Apps

`build-all.ps1` builds the complete host-compiled dcc application cohort and
installs each generated `.COM` file into the default CP/M C: disk image.

Stop the local emulator before updating its disk image, then run:

```powershell
pwsh ./Apps/BUILDALL/build-all.ps1
```

The script discovers every immediate `Apps` subdirectory containing
`build-app.ps1`, sorts the scripts by app name, and invokes them serially. Each
app wrapper compiles through `dccmake` and installs its fresh output through
`scripts/cpm-install.ps1`. The build stops on the first error and prints
`BUILD ALL PASS` only after every app succeeds.

Apps without `build-app.ps1` are not part of the dcc cohort. This includes
applications that currently have only BDS C, MAC, ASM, or BASIC build sources.
