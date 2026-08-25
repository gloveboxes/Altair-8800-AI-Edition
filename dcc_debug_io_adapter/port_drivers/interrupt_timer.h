#pragma once

#include "dcc_debug_io_adapter.h"

#include <stdint.h>

#define INTERRUPT_TIMER_PORT 52

int interrupt_timer_init(const dcc_debug_io_host_services_t *host);
void interrupt_timer_close(void);
void interrupt_timer_output(uint8_t rate_hz);
uint8_t interrupt_timer_input(void);