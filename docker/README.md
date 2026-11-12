# Apple container CP/M

Local application testing and source debugging use
[z80-cpm-container](https://github.com/gloveboxes/z80-cpm-container) on
Apple silicon macOS. Check out the shared repository at
`../../z80-cpm-container` relative to this repository, or set
`Z80_CPM_CONTAINER` to its absolute path. Install the Apple `container` CLI,
Python 3, CMake, a C compiler and the VS Code C/C++ extension for debugging.

Build and install applications with `pwsh Apps/build-all.ps1`. Start the Apple
container service and run:

```sh
container system start
python3 scripts/z80_container.py start
```

The VS Code task **Altair: Start Apple Container CP/M** does the same. The
container always mounts this project's `disks/512kb` read/write, so CP/M
changes persist and installed apps are current. Its startup refreshes drive A
boot tracks and AUTORUN while preserving the other files. Stop the container
before building apps or modifying the images; do not run two sessions against
the writable disks. Use Ctrl+C to stop the session.

Input and output start in the task terminal. Open http://127.0.0.1:8080/ for
browser-exclusive I/O; disconnect to return to the console. Sound plays through
the host audio client. Options `--skip-build`, `--web-port`, `--sound-port`
and `--console-port` are passed to the shared launcher.

## Debugging

Open an app's C source, then select **DCC CP/M: Debug active C file in Apple
container** from the Run and Debug dropdown before pressing F5. VS Code remembers
the selected configuration; **ESP-IDF: Debug hardware (OpenOCD)** is for the
physical ESP32 and requires OpenOCD. The pre-launch task builds the app with
`-g` and prepares the shared image. A separate debug container mounts
`disks/512kb` read-only and
creates a fresh private snapshot; it installs the debug build on snapshot
drive B and leaves the project disks unchanged. Stop the writable interactive
session before starting a debug session so the snapshot is consistent.

Open http://127.0.0.1:8081/ for debug target output and keyboard input;
continue execution after a breakpoint before typing. The browser terminal
loads xterm scripts from a CDN. Set `--web-port` or `--sound-port` in the
launch configuration if the default debug ports are in use.

The ESP32 hardware uses its own disk format; local container tests use the
512 KiB images. The shared repository's local checkout must contain its
container sources; see its README for image and sound-client requirements.
