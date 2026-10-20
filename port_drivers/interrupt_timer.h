#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "io_ports.h"

#define INTERRUPT_TIMER_PORT IO_INTERRUPT_TIMER_PORT

void interrupt_timer_init(void);
void interrupt_timer_output(uint8_t rate_hz);
uint8_t interrupt_timer_input(void);