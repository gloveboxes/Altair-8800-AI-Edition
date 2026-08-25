# DCC Debug I/O Adapter

`dcc-debug-io-adapter` is the optional Altair I/O-port driver library for
`dcc-debug-host`. It owns the port map and the host implementations for time,
utility, weather, chat, file-transfer, environment, and interrupt-timer ports.
The debugger host itself has no compiled-in port assignments.

## Build and test

```sh
cmake -S dcc_debug_io_adapter -B dcc_debug_io_adapter/build
cmake --build dcc_debug_io_adapter/build
ctest --test-dir dcc_debug_io_adapter/build --output-on-failure
```

The library is generated as:

- macOS: `build/libdcc-debug-io-adapter.dylib`
- Linux: `build/libdcc-debug-io-adapter.so`
- Windows: `build/dcc-debug-io-adapter.dll`

The example environment file is copied to `build/altair_env.txt`. Keep API
keys in that ignored build copy or pass another private file to the host with
`--env-file`.

## ABI

The versioned C ABI is declared in
`include/dcc_debug_io_adapter.h`. A library exports one symbol:

```c
int dcc_debug_io_adapter_init(
    const dcc_debug_io_adapter_config_t *config,
    dcc_debug_io_adapter_t *adapter,
    char *error,
    size_t error_size);
```

Initialization receives the environment-file path, the debugger session's
native files root, and host interrupt services. It returns port input/output
callbacks and a required `close` callback. The host calls `close` before
unloading the library so adapters can stop threads and release resources.

The host-service table lets an adapter register interrupt providers without
linking against debugger internals. Poll callbacks run on the emulator thread;
`raise_interrupt` and `clear_interrupt` may be called through the supplied
service table.

ABI v2 also provides optional terminal input and poll callbacks. This adapter
uses them to normalize ANSI terminal keys to the CP/M control-key convention:

- Up/Down/Right/Left: Ctrl-E/Ctrl-X/Ctrl-D/Ctrl-S
- Insert/Delete: Ctrl-O/Ctrl-G
- Page Up/Page Down: Ctrl-R/Ctrl-V
- Backspace/Delete: Ctrl-H

A standalone Escape is emitted after a 30 ms grace period so it can be
distinguished from the start of an ANSI sequence. These mappings are adapter
policy; a generic host without this adapter passes terminal bytes through.

## Direct use

```sh
"$DCC_DIR/build/dcc_debug_host/dcc-debug-host" --interpreter=mi \
  --io-adapter ./dcc_debug_io_adapter/build/libdcc-debug-io-adapter.dylib \
  --env-file ./dcc_debug_io_adapter/build/altair_env.txt
```

Omit `--io-adapter` to run the generic debugger with unmapped input ports
returning zero and output ports ignored.
