/* Host-side adapter for the ESP32 websocket_console module. See header for
   rationale. The CPU monitor (and any other publish_message consumer) routes
   its output here. When the browser terminal is active the bytes are queued
   for connected WebSocket clients; otherwise they fall back to stdout so the
   stdio terminal still shows them. */

#include "websocket_console.h"
#include "host_platform.h"
#include "web_terminal.h"

void websocket_console_enqueue_output(uint8_t value)
{
    if (web_terminal_active())
    {
        web_terminal_tx_byte(value);
        return;
    }
    (void)host_terminal_write_byte(value);
}

bool websocket_console_has_clients(void)
{
    return web_terminal_active() && web_terminal_has_clients();
}

void websocket_console_clear_queues(void)
{
    if (web_terminal_active())
    {
        web_terminal_clear_tx();
    }
}
