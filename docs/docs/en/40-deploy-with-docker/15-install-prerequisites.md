Install Apple's `container` CLI on an Apple silicon Mac, plus Python 3, CMake
and a C compiler. Check out `gloveboxes/z80-cpm-container` at
`../../z80-cpm-container` relative to this project, or set `Z80_CPM_CONTAINER`
to its absolute path. Initialize the project submodules:

```sh
git submodule update --init --recursive
container system start
```

The first build needs network access for Debian packages and the miniaudio
header. An audio output device is required by the host sound client.
