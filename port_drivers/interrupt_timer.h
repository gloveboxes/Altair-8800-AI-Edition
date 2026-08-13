#pragma once

#include <stdbool.h>
#include <stdint.h>

#define INTERRUPT_TIMER_PORT 52

void interrupt_timer_init(void);
void interrupt_timer_output(uint8_t rate_hz);
uint8_t interrupt_timer_input(void);