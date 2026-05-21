#ifndef FRONT_PANEL_KIT_H
#define FRONT_PANEL_KIT_H

#include <stdbool.h>
#include <stdint.h>

#include "intel8080.h"

bool front_panel_kit_init(void);
void front_panel_kit_update(const intel8080_t *cpu);
uint8_t front_panel_kit_take_command(void);
void front_panel_kit_set_brightness(int brightness);

#endif