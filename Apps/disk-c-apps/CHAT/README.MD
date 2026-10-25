# CHAT.COM — Setup Guide

`CHAT.COM` is a CP/M client for OpenAI Chat Completions and any compatible
endpoint (Ollama, llama.cpp server, LM Studio, vLLM, etc.). It talks to the
host through Intel 8080 I/O ports backed by libcurl on the desktop emulator
and by the ESP-IDF HTTP client on the ESP32 firmware.

The dcc C11 client links `../COMMON/DXSVC.C` and uses ROMLESS-SVC/1 on ports
2/3/4. ENV requests are self-contained length-delimited group `0x30`
transactions. Chat group `0x50` receives APPEND blocks of at most 2047 bytes,
then an explicit SUBMIT; STATUS and READ_CHUNK transactions consume up to
256 response bytes at a time. No read-port trigger or legacy NUL-terminated
transport is used. Timer delays use millisecond timer 0 in group `0x10`.
Transport failures are diagnosed and reset by the shared helper.

Build with `dccmake` for `build/CHAT.COM` without installing a disk image.
Run `sh ./test-services.sh` for repeatable native tests of APPEND sizing,
ENV payloads, waiting/status/chunk handling, response formatting and SUBMIT
errors, plus thinking defaults, provider switching, exact JSON and buffer
bounds. These stub service operations; they do not test live HTTP streaming.

All configuration is read from the Altair env store via the `ENV` app
(set with `ENV NAME=value`) or, on the device, from the web config portal.
Both sides share the NVS namespace `ENVIRONMENT` on the ESP32, and
`altair_local/altair_env.txt` on the host emulator.

## Required environment variables

### ESP32 firmware (`port_drivers/chat_io.c`)

| Variable | When | Purpose |
|---|---|---|
| `CHAT_PROVIDER`    | always | `openai` (uses `https://api.openai.com/v1/chat/completions`) or `compatible` (uses `CHAT_ENDPOINT`) |
| `CHAT_ENDPOINT`    | `CHAT_PROVIDER=compatible` | Full URL, e.g. `http://192.168.1.20:11434/v1/chat/completions` |
| `CHAT_OPENAI_KEY`  | `CHAT_PROVIDER=openai` (optional for `compatible`) | Bearer token sent in the `Authorization` header |

### Host emulator (`altair_local/PortDrivers/host_chat_io.c`)

The host emulator now honors the same `CHAT_PROVIDER` / `CHAT_ENDPOINT` /
`CHAT_OPENAI_KEY` variables as the ESP32 firmware. The driver picks one URL
at startup (logged as `[Chat] provider=... url=... key=...`). To change
provider/endpoint, edit `altair_local/altair_env.txt` and relaunch
`altair-local`.

### Read by `CHAT.COM` itself (both platforms, via `e_get`)

| Variable | Default | Purpose |
|---|---|---|
| `CHAT_MODEL`       | `gemma3:1b` | Model name passed to the API |
| `CHAT_MAX_TOKENS`  | `1024`      | `max_tokens` in the request |
| `CHAT_TEMPERATURE` | `0.7`       | Sampling temperature |
| `CHAT_THINK`       | `false`     | Ollama thinking control for `compatible`/`ollama`; ignored for `openai` |

These can also be set in the env store with `ENV NAME=value`; values are
read fresh each time `CHAT.COM` starts.

### Ollama thinking control (CHAT 1.7-c11)

With `CHAT_PROVIDER=compatible` (the usual Ollama setup), thinking is disabled
by default. The Pico also accepts the explicit `CHAT_PROVIDER=ollama` alias;
the ESP32 and desktop drivers still use `compatible`.

| `CHAT_THINK` | OpenAI-compatible request field | Behavior |
|---|---|---|
| Absent, empty, or `false` | `"reasoning_effort":"none"` | Request a direct answer without a thinking phase |
| `true` | `"reasoning_effort":"medium"` | Enable thinking for models with boolean controls; request medium effort where supported |
| `default` | Field omitted | Leave thinking to the server/model default |

Only models that support disabling thinking can honor `none`. This uses the
OpenAI-compatible `/v1/chat/completions` parameter, **not** the native Ollama
`think` parameter. The host/Pico/ESP32 driver forwards the JSON unchanged.
Reasoning output remains hidden; only the final answer is displayed.

`CHAT_THINK` and `CHAT_PROVIDER` are re-read before each turn, so switching
back to OpenAI never carries a stale Ollama option. OpenAI (including an
absent/empty provider) does not read or emit `CHAT_THINK`. Missing/empty
`CHAT_THINK` is seeded as `false` for compatible providers, as with the other
first-run defaults. Values are lowercase `false`, `true`, or `default`;
invalid values and ENV read/write failures stop the request rather than
silently using another mode. The usual service helper may print a NOT_FOUND
diagnostic while seeding a missing key on first use.

For non-Ollama compatible servers that do not implement `reasoning_effort`,
set `ENV CHAT_THINK=default` to preserve their original request format.
Thinking still counts toward CHAT's token budget, and a model that produces
no answer content for approximately 30 seconds can hit the client's idle
timeout.

Examples, entered at the CP/M prompt before starting CHAT (or changed from a
second console between turns):

```text
ENV CHAT_THINK=false
ENV CHAT_THINK=true
ENV CHAT_THINK=default
```

These are alternatives, not a sequence to run. No Pico firmware update is
needed for this setting; install the rebuilt `build/CHAT.COM` and keep the
existing `CHAT.SYS`. Building does not install a disk image or change a board.

## Example: OpenAI

Set inside CP/M:

```text
A>ENV CHAT_PROVIDER=openai
A>ENV CHAT_OPENAI_KEY=sk-proj-xxxxxxxxxxxxxxxxxxxxxxxx
A>ENV CHAT_MODEL=gpt-4o-mini
A>CHAT
```

The same `CHAT_OPENAI_KEY` name is used on both the ESP32 firmware and the
host emulator (set it in `altair_local/altair_env.txt` for the host).

## Example: Ollama on the LAN

```text
A>ENV CHAT_PROVIDER=compatible
A>ENV CHAT_ENDPOINT=http://192.168.1.20:11434/v1/chat/completions
A>ENV CHAT_MODEL=gemma3:1b
A>CHAT
```

For the Ollama server itself to accept connections from the Altair and use a
useful context window, configure it as below.

---

# Ollama Environment Configuration Guide

This guide explains how to install Ollama on Linux and configure environment variables for the server (Systemd) to enable network access and a larger context window.

## 1. Install Ollama on Linux

The official install script downloads the latest Ollama release, installs the `ollama` binary, and registers it as a systemd service.

Run the following in your terminal:

```bash
curl -fsSL https://ollama.com/install.sh | sh
```

Verify the installation:

```bash
ollama --version
systemctl status ollama
```

> **Note:** The installer creates an `ollama` system user and enables the `ollama.service` unit so the server starts automatically on boot.

## 2. Edit the Service Configuration

Use the `systemctl edit` command to create a persistent override for the Ollama service. This ensures your settings are not lost when Ollama is updated.

Run the following command in your terminal:

```bash
sudo systemctl edit ollama.service
```

## 3. Add Environment Variables

An editor will open (usually Nano). Paste the following block exactly as shown:

```ini
[Service]
Environment="OLLAMA_HOST=0.0.0.0"
Environment="OLLAMA_CONTEXT_LENGTH=8192"
```

- **To Save:** Press `Ctrl + O`, then `Enter`.
- **To Exit:** Press `Ctrl + X`.

### Key Settings

- **`OLLAMA_HOST=0.0.0.0`** — Allows the server to accept connections from any device on your local network (e.g., your laptop or tablet).
- **`OLLAMA_CONTEXT_LENGTH=8192`** — Sets the default context window to 8,192 tokens for all models.

## 4. Restart the Service

To apply the changes, you must reload the systemd manager configuration and restart the service:

```bash
# Reload systemd manager configuration
sudo systemctl daemon-reload

# Restart the Ollama service
sudo systemctl restart ollama
```

## 5. Verify Success

To verify that the settings have been applied correctly, check the status of the service:

```bash
systemctl status ollama
```

You can also verify the network listener is active on all interfaces:

```bash
netstat -tulpn | grep 11434
```

If successful, you will see `0.0.0.0:11434` in the output.
