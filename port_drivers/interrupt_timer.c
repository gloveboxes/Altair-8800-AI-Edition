#include "interrupt_timer.h"

#include "interrupt_controller.h"

#include <stdatomic.h>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#else
#include <time.h>
#endif

static atomic_uchar configured_rate;
static interrupt_provider_id_t provider_id = INTERRUPT_PROVIDER_INVALID;

#ifdef ESP_PLATFORM
static esp_timer_handle_t periodic_timer;

static void timer_expired(void *argument)
{
    (void)argument;
    interrupt_controller_raise(provider_id);
}
#else
static struct timespec next_expiration;
static void timer_poll(void *context);

static uint64_t monotonic_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

static void set_next_expiration(uint8_t rate_hz)
{
    uint64_t next_us = monotonic_us() + 1000000u / rate_hz;
    next_expiration.tv_sec = (time_t)(next_us / 1000000u);
    next_expiration.tv_nsec = (long)(next_us % 1000000u) * 1000L;
}
#endif

void interrupt_timer_init(void)
{
    const interrupt_provider_config_t provider = {
        .data_bus = 0xff,
#ifndef ESP_PLATFORM
        .poll = timer_poll,
#endif
    };

    atomic_store_explicit(&configured_rate, 0, memory_order_relaxed);
    provider_id = INTERRUPT_PROVIDER_INVALID;
    if (!interrupt_controller_register(&provider, &provider_id))
        return;
#ifdef ESP_PLATFORM
    const esp_timer_create_args_t args = {
        .callback = timer_expired,
        .name = "z80_int"
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &periodic_timer));
#endif
}

void interrupt_timer_output(uint8_t rate_hz)
{
#ifdef ESP_PLATFORM
    if (esp_timer_is_active(periodic_timer))
        ESP_ERROR_CHECK(esp_timer_stop(periodic_timer));
#endif
    interrupt_controller_clear(provider_id);
    atomic_store_explicit(&configured_rate, rate_hz, memory_order_relaxed);
    if (rate_hz == 0)
        return;

#ifdef ESP_PLATFORM
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000000u / rate_hz));
#else
    set_next_expiration(rate_hz);
#endif
}

uint8_t interrupt_timer_input(void)
{
    return atomic_load_explicit(&configured_rate, memory_order_relaxed);
}

#ifndef ESP_PLATFORM
static void timer_poll(void *context)
{
    (void)context;
    uint8_t rate_hz = interrupt_timer_input();
    if (rate_hz != 0)
    {
        uint64_t next_us = (uint64_t)next_expiration.tv_sec * 1000000u
                         + (uint64_t)next_expiration.tv_nsec / 1000u;
        if (monotonic_us() >= next_us)
        {
            interrupt_controller_raise(provider_id);
            set_next_expiration(rate_hz);
        }
    }
}
#endif