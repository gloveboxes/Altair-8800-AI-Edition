The launcher builds the local Apple container image and the host sound client:

```sh
python3 docker/start.py
```

The shared image contains the SBC emulator and browser terminal. Build the
image and host sound client without launching it:

```sh
python3 scripts/z80_container.py prepare
```

The shared repository's sound client uses miniaudio for audio output. Container
orchestration currently targets Apple containers on macOS.
