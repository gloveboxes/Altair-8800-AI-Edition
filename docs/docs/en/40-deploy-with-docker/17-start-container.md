From the repository root, run:

```sh
python3 scripts/z80_container.py start
```

Or run the VS Code task **Run interactive CP/M emulator**. CP/M boots in
the task terminal. Open <http://127.0.0.1:8080/> to give the browser exclusive
control; it returns to the task terminal when the browser disconnects. Press
Enter after a handover to display the current prompt. Ctrl+C stops the sound
client and removes the container.

Before CP/M starts, all four `disks/512kb/` images are checked for exact 512 KiB
size and filesystem integrity using the container's `fsck.cpm -n`. Failure
reports the disk and checker output, stops startup and leaves images unchanged;
the checker does not repair filesystems. Successful validation messages for
A-D appear in the task terminal before the CP/M prompt.

The project's `disks/512kb` is always mounted read/write. Drive A boot tracks
and `AUTORUN.COM` are refreshed on startup; other files persist. These are the
same base disks used by `Apps/build-all.ps1`, not temporary session copies.
You can edit, create and delete files from CP/M, then stop with Ctrl+C.
Stop the emulator before host-side builds or disk copies.
See [editing and copying base disks to the Z80 SBC](../90-appendices/10-build-and-install-cpm-apps.md#edit-base-disks-and-copy-to-the-z80-sbc)
for build-and-copy, copy-only and VS Code tasks.

`--web-port`, `--sound-port` and
`--console-port` choose different localhost ports; `--skip-build` reuses the
existing image.
