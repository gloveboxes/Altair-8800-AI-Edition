/**
 * @file websocket_console.c
 * @brief WebSocket console implementation for Altair 8800
 *
 * Provides cross-core communication between WebSocket server (Core 0)
 * and Altair emulator (Core 1) using FreeRTOS queues.
 * Uses a dedicated low-priority task for TX to avoid blocking esp_timer.
 */

#include "websocket_console.h"
#include "websocket_server.h"
#include "terminal_input.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "WS_Console";

// Buffer capacity - absorbs burst terminal output (e.g., screen clears,
// listings) between TX-task drains. The reader empties the whole buffer on
// each wake, so this only needs to cover a single burst, not sustained
// throughput. Measured peak occupancy under heavy load is ~300 bytes, so 1024
// leaves roughly 3x headroom. Allocated from internal RAM, so kept modest.
#define WS_TX_QUEUE_DEPTH   1024   // Output to WebSocket client

// Maximum bytes copied per WebSocket send. Only sizes a stack buffer and the
// per-send chunk; the TX task loops until the stream is empty, so a small
// value does NOT cap throughput.
#define WS_TX_BATCH_SIZE    256

// Timer interval for batched output (microseconds)
// 10ms for high throughput while still batching efficiently
#define WS_TX_TIMER_INTERVAL_US  (10 * 1000)  // 10ms

// Ping interval to keep WebSocket connections alive (microseconds)
#define WS_PING_INTERVAL_US  (30 * 1000 * 1000)  // 30 seconds

// TX task stack size and priority (higher than other app tasks, still below esp_timer)
#define WS_TX_TASK_STACK    4096
#define WS_TX_TASK_PRIORITY 11     // Keep below esp_timer (22)
#define WS_TX_TASK_CORE     0      // Pin to Core 0 to avoid emulator core

// Byte stream for emulator -> WebSocket output. A StreamBuffer (single writer:
// the emulator task on Core 1; single reader: the TX task on Core 0) lets the
// reader drain a whole batch in one call instead of one xQueueReceive per byte.
// Input is routed through the shared terminal_input queue so BLE keyboard and
// WebSocket clients share a single consumer in the emulator loop.
static StreamBufferHandle_t s_tx_stream = NULL;  // Emulator -> WebSocket

// Semaphore to wake TX task
static SemaphoreHandle_t s_tx_sem = NULL;

// TX task handle
static TaskHandle_t s_tx_task = NULL;

// Timer for batched output (just signals the TX task)
static esp_timer_handle_t s_tx_timer = NULL;

// Timer for WebSocket keepalive pings
static esp_timer_handle_t s_ping_timer = NULL;

// Initialization flag
static bool s_initialized = false;

// Flag to signal TX task to send a ping (set by timer, cleared by TX task)
static volatile bool s_ping_pending = false;

// Flag to ask the TX task to flush stale output. Set by connect/disconnect
// callbacks (which run on the httpd task); the StreamBuffer is drained only by
// the reader (TX task), so flushing here keeps the single-reader contract and
// avoids a cross-task reset racing with the emulator's writes.
static volatile bool s_flush_pending = false;

/**
 * @brief Drain and discard any buffered TX output.
 *
 * MUST be called only from the TX task (the StreamBuffer's single reader). A
 * reader drain is always safe against a concurrent writer; a foreign-task
 * reset would not be.
 */
static void flush_tx_stream(void)
{
    if (s_tx_stream) {
        uint8_t scratch[64];
        while (xStreamBufferReceive(s_tx_stream, scratch, sizeof(scratch), 0) > 0) {
            // discard stale bytes
        }
    }
}

/**
 * @brief TX task - sends batched data to WebSocket client
 * 
 * Runs at lower priority than esp_timer to avoid starving system tasks.
 * Woken by timer or when queue has data.
 */
static void tx_task(void* arg)
{
    (void)arg;
    uint8_t buffer[WS_TX_BATCH_SIZE];

    while (1) {
        // Wait for timer signal or timeout (for periodic check)
        xSemaphoreTake(s_tx_sem, pdMS_TO_TICKS(20));

        if (!s_initialized || !s_tx_stream) {
            continue;
        }

        // Handle a deferred flush request from connect/disconnect. Done here
        // (the single reader) so it never races with the emulator's writes.
        if (s_flush_pending) {
            s_flush_pending = false;
            flush_tx_stream();
        }

        // Don't bother if no client
        if (!websocket_console_has_clients()) {
            flush_tx_stream();
            s_ping_pending = false;
            continue;
        }

        // Send ping if requested (from timer callback)
        // Do this from TX task to avoid racing with data sends
        if (s_ping_pending) {
            s_ping_pending = false;
            websocket_server_send_ping();
        }

        // Drain everything currently buffered, sending in batch-sized chunks.
        // Looping here decouples throughput from WS_TX_BATCH_SIZE so the batch
        // (and its stack buffer) can stay small.
        size_t count;
        while ((count = xStreamBufferReceive(s_tx_stream, buffer, WS_TX_BATCH_SIZE, 0)) > 0) {
            websocket_server_broadcast(buffer, count);
            // Yield between full chunks so other tasks are not starved.
            if (count == WS_TX_BATCH_SIZE) {
                taskYIELD();
            }
        }
    }
}

/**
 * @brief Timer callback - just signals TX task to wake up
 * 
 * Runs in esp_timer task but does minimal work (just gives semaphore).
 */
static void tx_timer_callback(void* arg)
{
    (void)arg;
    if (s_tx_sem) {
        xSemaphoreGive(s_tx_sem);
    }
}

/**
 * @brief Timer callback for WebSocket keepalive pings
 * 
 * Sets flag and wakes TX task to send ping (avoids racing with data sends).
 */
static void ping_timer_callback(void* arg)
{
    (void)arg;

    if (!s_initialized) {
        return;
    }

    if (websocket_console_has_clients()) {
        s_ping_pending = true;
        if (s_tx_sem) {
            xSemaphoreGive(s_tx_sem);  // Wake TX task to send ping
        }
    }
}

void websocket_console_init(void)
{
    if (s_initialized) {
        return;
    }

    // Create TX stream buffer. Trigger level 1: the reader uses non-blocking
    // receives, so the trigger level (which only affects blocked readers) is
    // immaterial; 1 is the conventional default.
    s_tx_stream = xStreamBufferCreate(WS_TX_QUEUE_DEPTH, 1);

    if (!s_tx_stream) {
        ESP_LOGE(TAG, "Failed to create TX stream buffer");
        return;
    }

    // Create TX semaphore
    s_tx_sem = xSemaphoreCreateBinary();
    if (!s_tx_sem) {
        ESP_LOGE(TAG, "Failed to create TX semaphore");
        vStreamBufferDelete(s_tx_stream);
        s_tx_stream = NULL;
        return;
    }

    // Create TX task at lower priority than esp_timer. Keep its stack in PSRAM
    // so BLE/WiFi/internal-DMA users do not starve internal RAM during boot.
    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(tx_task, "ws_tx", WS_TX_TASK_STACK, NULL,
                                                     WS_TX_TASK_PRIORITY, &s_tx_task,
                                                     WS_TX_TASK_CORE,
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS) {
        ESP_LOGW(TAG, "Failed to create TX task in PSRAM; trying internal RAM");
        ret = xTaskCreatePinnedToCore(tx_task, "ws_tx", WS_TX_TASK_STACK, NULL,
                                      WS_TX_TASK_PRIORITY, &s_tx_task,
                                      WS_TX_TASK_CORE);
    }
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TX task (free internal=%lu largest internal=%lu free psram=%lu largest psram=%lu)",
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        vSemaphoreDelete(s_tx_sem);
        vStreamBufferDelete(s_tx_stream);
        s_tx_sem = NULL;
        s_tx_stream = NULL;
        return;
    }

    // Create TX batching timer (just signals the task)
    const esp_timer_create_args_t timer_args = {
        .callback = tx_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ws_tx_timer"
    };

    esp_err_t err = esp_timer_create(&timer_args, &s_tx_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create TX timer: %s", esp_err_to_name(err));
        vTaskDelete(s_tx_task);
        vSemaphoreDelete(s_tx_sem);
        vStreamBufferDelete(s_tx_stream);
        s_tx_task = NULL;
        s_tx_sem = NULL;
        s_tx_stream = NULL;
        return;
    }

    // Create ping timer
    const esp_timer_create_args_t ping_timer_args = {
        .callback = ping_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ws_ping_timer"
    };

    err = esp_timer_create(&ping_timer_args, &s_ping_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ping timer: %s", esp_err_to_name(err));
        esp_timer_delete(s_tx_timer);
        vTaskDelete(s_tx_task);
        vSemaphoreDelete(s_tx_sem);
        vStreamBufferDelete(s_tx_stream);
        s_tx_timer = NULL;
        s_tx_task = NULL;
        s_tx_sem = NULL;
        s_tx_stream = NULL;
        return;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Console initialized (TX=%d, timer=%dms, task_prio=%d)",
             WS_TX_QUEUE_DEPTH, WS_TX_TIMER_INTERVAL_US / 1000,
             WS_TX_TASK_PRIORITY);
}

bool websocket_console_start_server(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Console not initialized");
        return false;
    }

    if (!websocket_server_start()) {
        return false;
    }

    // Start the TX batching timer
    if (s_tx_timer) {
        esp_err_t err = esp_timer_start_periodic(s_tx_timer, WS_TX_TIMER_INTERVAL_US);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start TX timer: %s", esp_err_to_name(err));
            websocket_server_stop();
            return false;
        }
        ESP_LOGI(TAG, "TX batching timer started (%dms interval)", WS_TX_TIMER_INTERVAL_US / 1000);
    }

    // Start the ping timer
    if (s_ping_timer) {
        esp_err_t err = esp_timer_start_periodic(s_ping_timer, WS_PING_INTERVAL_US);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start ping timer: %s", esp_err_to_name(err));
            // Non-fatal - continue without pings
        } else {
            ESP_LOGI(TAG, "Ping timer started (%ds interval)", WS_PING_INTERVAL_US / 1000000);
        }
    }

    return true;
}

bool websocket_console_has_clients(void)
{
    return websocket_server_get_client_count() > 0;
}

void websocket_console_enqueue_output(uint8_t value)
{
    if (!s_initialized || !s_tx_stream) {
        return;
    }

    // No browser terminal is connected. This is a normal idle state; stale
    // buffered output is flushed by the TX task on connect/disconnect.
    if (!websocket_console_has_clients()) {
        return;
    }

    // Non-blocking write. If the buffer is full the byte is dropped (real-time
    // terminal data). The StreamBuffer is single-writer/single-reader, so we
    // must not read here to "drop oldest" - that would race the TX task reader;
    // dropping the newest byte under a full buffer is the accepted tradeoff.
    (void)xStreamBufferSend(s_tx_stream, &value, 1, 0);
}

void websocket_console_clear_queues(void)
{
    // Only the TX stream is owned by this module; input is queued into the
    // shared terminal_input queue and must not be flushed here (doing so
    // would drop bytes typed on the BLE keyboard).
    //
    // The stream is drained only by its single reader (the TX task), so request
    // a deferred flush instead of touching the buffer from this (foreign) task.
    s_flush_pending = true;
    if (s_tx_sem) {
        xSemaphoreGive(s_tx_sem);
    }
}

/**
 * @brief Handle incoming WebSocket data (called from WebSocket server)
 *
 * This function is called by the WebSocket server when data is received
 * from a client. Bytes are pushed into the shared terminal_input queue so
 * the emulator on Core 1 sees them alongside BLE keyboard input.
 *
 * @param data Pointer to received data
 * @param len Length of received data
 */
void websocket_console_handle_rx(const uint8_t* data, size_t len)
{
    if (!s_initialized || !data || len == 0) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t ch = data[i];

        // Convert newline to carriage return (terminal convention)
        if (ch == '\n') {
            ch = '\r';
        }

        terminal_input_enqueue(ch);
    }
}

/**
 * @brief Handle client connect event
 */
void websocket_console_on_connect(void)
{
    // Ask the TX task to discard any stale data before this client starts, and
    // wake it immediately so a freshly connected client begins draining without
    // waiting for the next timer tick.
    s_flush_pending = true;
    if (s_tx_sem) {
        xSemaphoreGive(s_tx_sem);
    }
}

/**
 * @brief Handle client disconnect event
 */
void websocket_console_on_disconnect(void)
{
    // Drain any stale outbound data; inbound bytes are owned by the shared
    // terminal_input queue and are intentionally left alone.
    websocket_console_clear_queues();
}
