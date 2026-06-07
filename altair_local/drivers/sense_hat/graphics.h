/* 8x8 LED matrix graphics helpers for the Raspberry Pi Sense HAT.
 *
 * Ported from the Altair-8800-Emulator reference project
 * (src/Drivers/pi_sense_hat/graphics.h).
 */

#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "led_panel.h"

uint8_t gfx_reverse_byte(uint8_t data);
void gfx_bitmap_to_rgb(uint8_t bitmap[8], uint16_t *panel_buffer, size_t buffer_len);
void gfx_load_character(uint8_t character, uint8_t bitmap[8]);
void gfx_reverse_panel(unsigned char A[8]);
void gfx_rotate_counterclockwise(unsigned char A[8], uint32_t m, uint32_t n, unsigned char B[8]);
void gfx_set_color(uint8_t color);
