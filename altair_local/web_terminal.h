/**
 * @file web_terminal.h
 * @brief Browser terminal bridge for the local Altair runner.
 *
 * Mirrors the ESP32 firmware's WebSocket terminal so the same, unmodified
 * web terminal UI (terminal/index.html) can drive the host build of the
 * emulator.
 *
 * Two cooperating servers run on background threads:
 *   1. A tiny HTTP server on the public port that serves terminal/index.html
 *      and reverse-proxies WebSocket upgrade requests (the page connects to
 *      ws://<host>:<port>/ws) to a loopback wsServer instance.
 *   2. wsServer (Theldus, vendored as a git submodule) bound to 127.0.0.1 on
 *      port+1, which performs all WebSocket protocol work.
 *
 * Keeping both behind one public port is required because the web terminal
 * derives its WebSocket URL from the page's own host and port.
 *
 * The emulator (main thread) pushes output via web_terminal_tx_byte() and
 * pulls input via web_terminal_rx_byte(); both are non-blocking and backed by
 * ring buffers, exactly like the ESP32 websocket_console queues.
 *
 * On Windows this module compiles to stubs that report the web terminal as
 * unavailable, so the local runner keeps its stdio terminal there.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Start the web terminal (HTTP UI server + WebSocket bridge).
 *
 * @param repo_root  Absolute path to the repository root; terminal/index.html
 *                   is loaded relative to it.
 * @param port       Public TCP port for the HTTP UI and WebSocket endpoint.
 * @return true on success, false on failure (or on platforms without support).
 */
bool web_terminal_start(const char *repo_root, uint16_t port);

/**
 * @brief Stop the web terminal and release its listening socket/threads.
 */
void web_terminal_stop(void);

/**
 * @brief Whether the web terminal is running (servers started).
 */
bool web_terminal_active(void);

/**
 * @brief Whether at least one browser terminal is currently connected.
 */
bool web_terminal_has_clients(void);

/**
 * @brief Queue one output byte for transmission to connected browsers.
 *
 * Non-blocking. Bytes are dropped when no client is connected (and the queue
 * is drained on connect, mirroring the ESP32 behaviour).
 */
void web_terminal_tx_byte(uint8_t value);

/**
 * @brief Drop all pending output bytes.
 */
void web_terminal_clear_tx(void);

/**
 * @brief Dequeue one input byte received from a browser terminal.
 *
 * @param out  Receives the byte when one is available.
 * @return true if a byte was returned, false if the input queue is empty.
 */
bool web_terminal_rx_byte(uint8_t *out);
