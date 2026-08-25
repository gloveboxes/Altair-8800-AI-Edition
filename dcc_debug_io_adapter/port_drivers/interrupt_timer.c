#include "interrupt_timer.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static dcc_debug_io_host_services_t host_services;
static dcc_debug_io_interrupt_id_t provider_id;
static uint8_t configured_rate;
static uint64_t next_expiration_us;
static int initialized;
static void timer_poll(void *context);

static uint64_t monotonic_us(void)
{
#ifdef _WIN32
    return GetTickCount64() * 1000u;
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
#endif
}

static void set_next_expiration(uint8_t rate_hz)
{
    next_expiration_us = monotonic_us() + 1000000u / rate_hz;
}

int interrupt_timer_init(const dcc_debug_io_host_services_t *host)
{
    host_services = *host;
    configured_rate = 0;
    if (!host_services.register_interrupt(host_services.context, 0xff,
                                          timer_poll, NULL, &provider_id))
        return 0;
    initialized = 1;
    return 1;
}

void interrupt_timer_close(void)
{
    if (initialized)
        host_services.clear_interrupt(host_services.context, provider_id);
    configured_rate = 0;
    initialized = 0;
}

void interrupt_timer_output(uint8_t rate_hz)
{
    if (!initialized)
        return;
    host_services.clear_interrupt(host_services.context, provider_id);
    configured_rate = rate_hz;
    if (rate_hz == 0)
        return;
    set_next_expiration(rate_hz);
}

uint8_t interrupt_timer_input(void)
{
    return configured_rate;
}

static void timer_poll(void *context)
{
    (void)context;
    uint8_t rate_hz = interrupt_timer_input();
    if (rate_hz != 0 && monotonic_us() >= next_expiration_us)
    {
        host_services.raise_interrupt(host_services.context, provider_id);
        set_next_expiration(rate_hz);
    }
}