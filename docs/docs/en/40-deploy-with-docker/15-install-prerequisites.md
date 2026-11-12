Install Apple's `container` CLI on an Apple silicon Mac, plus Python 3, CMake
and a C compiler. The shared container is the `external/z80-cpm-container`
submodule. Set `Z80_CPM_CONTAINER` to an alternate checkout when needed.
Initialize the project submodules:

```sh
git submodule update --init --recursive
container system start
```

The first build needs network access for Debian packages and the miniaudio
header. An audio output device is required by the host sound client.
