# C Programming

Current C applications use the host-based
[dcc compiler](https://davidly.github.io/dcc/) for CP/M 2.2 on the Z80.
Legacy BDS C applications, supporting SDK and compiler archives have been
removed. Assembly and BASIC applications remain supported.

dcc supports C89 plus CP/M-relevant C99/C11 features. It runs on Windows,
macOS and Linux, producing M80 assembly that is assembled and linked into a
CP/M `.COM` application.

Start with dcc's [toolchain setup](https://davidly.github.io/dcc/00-setup-toolchain/)
and [build/link reference](https://davidly.github.io/dcc/02-build-and-link/).
The target has 16-bit `int`/pointers and 32-bit `long`; use dcc's runtime
documentation rather than assuming desktop C library behavior.

## Build applications

Install PowerShell 7, dcc/ntvcm and cpmtools (`cpmrm` and `cpmcp`). Then use
the app's `build-app.ps1`, or the aggregate build:

```powershell
pwsh ./Apps/BUILDALL/build-all.ps1
```

Stop the emulator before installing into its disk image. For build-only
validation, run `dccmake` in the app directory instead. See
[Build and Install CP/M Apps](../90-appendices/10-build-and-install-cpm-apps.md)
for tool checks, installation and disk format details.

## Shared service client

Applications use `Apps/COMMON/DXSVC.C` and `DXSVC.H` for
[ROMless shared services](01-ROMless-IO-Mapping.md). Terminal/disk BIOS
interfaces are unchanged. Do not use the removed legacy service port numbers.

For an app directory under `Apps/`, a minimal example is:

```c
#include <stdio.h>
#include "../COMMON/DXSVC.H"

int main(void)
{
    char version[128];
    if (svc_get_text(SERVICE_GROUP_UTILITY, 3, version, sizeof(version)) < 0)
        return 1;
    printf("Emulator: %s\n", version);
    return 0;
}
```

Compile the shared implementation as a separate input, as the current app
configurations do:

```text
dcc-input=hello.c,../COMMON/DXSVC.C
dcc-output=HELLO
dcc-include-directory=${DCC_DIR},../COMMON
dcc-stack-bytes=768
```

The [shared client reference](https://github.com/gloveboxes/Altair-8800-AI-Edition/blob/main/Apps/COMMON/README.md)
documents framing, return/error conventions and regression tests.

See [Editing Files](01-Editing-files.md) for source editing, file transfer and
the retained CP/M assembly workflow.
