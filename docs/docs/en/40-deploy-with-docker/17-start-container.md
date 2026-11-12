From the repository root, run:

```sh
python3 docker/start.py
```

Or run the VS Code task **Altair: Start Apple Container CP/M**. CP/M boots in
the task terminal. Open <http://127.0.0.1:8080/> to give the browser exclusive
control; it returns to the task terminal when the browser disconnects. Press
Enter after a handover to display the current prompt. Ctrl+C stops the sound
client and removes the container.

The project's `disks/512kb` is always mounted read/write. Drive A boot tracks
and `AUTORUN.COM` are refreshed on startup. Stop the emulator before modifying
disks. `--web-port`, `--sound-port` and
`--console-port` choose different localhost ports; `--skip-build` reuses the
existing image.
