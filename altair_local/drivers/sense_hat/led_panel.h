/* Raspberry Pi Sense HAT 8x8 RGB LED panel driver (Linux framebuffer).
 *
 * Ported from the Altair-8800-Emulator reference project
 * (src/Drivers/pi_sense_hat/led_panel.h). Linux-only: the panel is exposed by
 * the rpisense-fb kernel driver as an RGB565 framebuffer (/dev/fb0 or /dev/fb1).
 */

#pragma once

#include <fcntl.h>
#include <linux/fb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PI_SENSE_8x8_BUFFER_SIZE (64 * sizeof(uint16_t))

bool pi_sense_hat_init(void);
void pi_sense_hat_close(void);
bool pi_sense_8x8_panel_update(uint16_t *panel_buffer, size_t buffer_len);
