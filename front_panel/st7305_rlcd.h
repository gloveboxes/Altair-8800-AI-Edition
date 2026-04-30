#ifndef ST7305_RLCD_H
#define ST7305_RLCD_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "panel_display.h"

#define ST7305_LCD_H_RES 400
#define ST7305_LCD_V_RES 300

esp_err_t st7305_rlcd_init(void);
void st7305_rlcd_fill_screen(panel_color_t color);
void st7305_rlcd_fill_rect(int x, int y, int w, int h, panel_color_t color);
void st7305_rlcd_draw_pixel(int x, int y, panel_color_t color);
void st7305_rlcd_draw_string(int x, int y, const char *str, panel_color_t fg_color, panel_color_t bg_color, int scale);
void st7305_rlcd_draw_string_small(int x, int y, const char *str, panel_color_t fg_color, panel_color_t bg_color);
void st7305_rlcd_draw_led_row(uint32_t bits, int num_leds, int x_start, int y,
                              int led_size, int spacing, panel_color_t on_color,
                              panel_color_t off_color, panel_color_t bg_color);
void st7305_rlcd_draw_led_span(uint32_t bits, int num_leds, int x_start, int y,
                               int led_size, int spacing, panel_color_t on_color,
                               panel_color_t off_color, panel_color_t bg_color,
                               int left_index, int right_index);
void st7305_rlcd_present(void);
void st7305_rlcd_set_backlight(int brightness);
bool st7305_rlcd_is_dirty(void);

#endif