#include "panel_display.h"

#include "sdkconfig.h"

#if CONFIG_ALTAIR_DISPLAY_ILI9341
#include "ili9341.h"
#else
#include "st7305_rlcd.h"
#endif

bool panel_display_init(void)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    return ili9341_init() == ESP_OK;
#else
    return st7305_rlcd_init() == ESP_OK;
#endif
}

int panel_display_width(void)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    return LCD_H_RES;
#else
    return ST7305_LCD_H_RES;
#endif
}

int panel_display_height(void)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    return LCD_V_RES;
#else
    return ST7305_LCD_V_RES;
#endif
}

bool panel_display_is_monochrome(void)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    return false;
#else
    return true;
#endif
}

void panel_display_fill_screen(panel_color_t color)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    ili9341_fill_screen(color);
#else
    st7305_rlcd_fill_screen(color);
#endif
}

void panel_display_fill_rect(int x, int y, int w, int h, panel_color_t color)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    ili9341_fill_rect(x, y, w, h, color);
#else
    st7305_rlcd_fill_rect(x, y, w, h, color);
#endif
}

void panel_display_draw_pixel(int x, int y, panel_color_t color)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    ili9341_draw_pixel(x, y, color);
#else
    st7305_rlcd_draw_pixel(x, y, color);
#endif
}

void panel_display_draw_string(int x, int y, const char *str, panel_color_t fg_color, panel_color_t bg_color, int scale)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    ili9341_draw_string(x, y, str, fg_color, bg_color, scale);
#else
    st7305_rlcd_draw_string(x, y, str, fg_color, bg_color, scale);
#endif
}

void panel_display_draw_string_small(int x, int y, const char *str, panel_color_t fg_color, panel_color_t bg_color)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    ili9341_draw_string_small(x, y, str, fg_color, bg_color);
#else
    st7305_rlcd_draw_string_small(x, y, str, fg_color, bg_color);
#endif
}

void panel_display_draw_led_row(uint32_t bits, int num_leds, int x_start, int y,
                                int led_size, int spacing, panel_color_t on_color,
                                panel_color_t off_color, panel_color_t bg_color)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    ili9341_draw_led_row(bits, num_leds, x_start, y, led_size, spacing,
                         on_color, off_color, bg_color);
#else
    st7305_rlcd_draw_led_row(bits, num_leds, x_start, y, led_size, spacing,
                             on_color, off_color, bg_color);
#endif
}

void panel_display_draw_led_span(uint32_t bits, int num_leds, int x_start, int y,
                                 int led_size, int spacing, panel_color_t on_color,
                                 panel_color_t off_color, panel_color_t bg_color,
                                 int left_index, int right_index)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    ili9341_draw_led_span(bits, num_leds, x_start, y, led_size, spacing,
                          on_color, off_color, bg_color, left_index, right_index);
#else
    st7305_rlcd_draw_led_span(bits, num_leds, x_start, y, led_size, spacing,
                              on_color, off_color, bg_color, left_index, right_index);
#endif
}

void panel_display_present(void)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    ili9341_wait_async();
#else
    st7305_rlcd_present();
#endif
}

void panel_display_set_backlight(int brightness)
{
#if CONFIG_ALTAIR_DISPLAY_ILI9341
    ili9341_set_backlight(brightness);
#else
    st7305_rlcd_set_backlight(brightness);
#endif
}