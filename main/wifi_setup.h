#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef void (*wifi_setup_network_status_callback_t)(bool wifi_connected);

/* Register an application callback for WiFi availability changes.
 * The callback is invoked from the WiFi setup task after connection attempts
 * determine whether network services should be enabled. */
void wifi_setup_set_network_status_callback(wifi_setup_network_status_callback_t callback);

/* Spawn the WiFi setup task. Connects to stored credentials, falls back to
 * captive-portal flow when none exist or connection fails, and brings up the
 * WebSocket terminal once an IP is obtained. Returns immediately; the task
 * self-deletes after station setup or keeps running while the portal is open. */
void wifi_setup_start(void);

/* Interactive boot-time WiFi credential manager (USB serial JTAG console).
 * Lets the user show, set, or clear stored WiFi credentials. It does not start
 * WiFi; the normal wifi_setup_start() task connects later using saved values. */
void wifi_setup_run_config_shell(void);

/* True once the WebSocket terminal has been started successfully. Read by
 * the main terminal_read/terminal_write loop on Core 1. */
bool wifi_setup_websocket_enabled(void);

#ifdef __cplusplus
}
#endif
