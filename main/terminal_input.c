/**
 * @file terminal_input.c
 * @brief Shared FreeRTOS ring buffer used by BLE keyboard and WebSocket producers.
 */

#include "terminal_input.h"

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

// Sized for burst paste / fast typing from either source. 128 bytes matches
// the previous per-source depths and is plenty for sparse human input.
#define TERMINAL_INPUT_BUFFER_SIZE 128

static RingbufHandle_t s_buffer = NULL;

void terminal_input_init(void)
{
    if (s_buffer != NULL)
    {
        return;
    }
    s_buffer = xRingbufferCreate(TERMINAL_INPUT_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
}

void terminal_input_enqueue(uint8_t value)
{
    if (s_buffer == NULL)
    {
        return;
    }

    if (xRingbufferSend(s_buffer, &value, sizeof(value), 0) != pdTRUE)
    {
        // Buffer full - drop oldest and try again so the most recent
        // keystrokes always make it through.
        size_t count;
        void *item = xRingbufferReceiveUpTo(s_buffer, &count, 0, 1);
        if (item != NULL)
        {
            vRingbufferReturnItem(s_buffer, item);
            xRingbufferSend(s_buffer, &value, sizeof(value), 0);
        }
    }
}

bool terminal_input_try_dequeue(uint8_t *value)
{
    if (s_buffer == NULL || value == NULL)
    {
        return false;
    }

    size_t count;
    uint8_t *item = xRingbufferReceiveUpTo(s_buffer, &count, 0, 1);
    if (item == NULL)
    {
        return false;
    }
    *value = item[0];
    vRingbufferReturnItem(s_buffer, item);
    return true;
}
