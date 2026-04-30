#include "st7305_rlcd.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "board_config.h"

static const char *TAG = "ST7305";

#define RLCD_PIN_DC   ALTAIR_ST7305_PIN_DC
#define RLCD_PIN_CS   ALTAIR_ST7305_PIN_CS
#define RLCD_PIN_SCK  ALTAIR_ST7305_PIN_SCK
#define RLCD_PIN_MOSI ALTAIR_ST7305_PIN_MOSI
#define RLCD_PIN_RST  ALTAIR_ST7305_PIN_RST

#define RLCD_SPI_HOST ALTAIR_ST7305_SPI_HOST
#define RLCD_SPI_HZ   (10 * 1000 * 1000)
#define RLCD_BUF_LEN  ((ST7305_LCD_H_RES * ST7305_LCD_V_RES) / 8)
#define RLCD_ROW_COUNT (ST7305_LCD_H_RES / 2)
#define RLCD_BLOCK_COUNT (ST7305_LCD_V_RES / 4)
#define RLCD_COLUMN_ADDR_START 0x12
#define RLCD_COLUMN_ADDR_END 0x2A
#define RLCD_ROW_ADDR_START 0x00
#define RLCD_ROW_ADDR_END 0xC7

static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static uint8_t *s_framebuffer = NULL;
static uint8_t *s_partial_buffer = NULL;
static bool s_dirty = false;
static SemaphoreHandle_t s_transfer_done_sem = NULL;
static volatile bool s_transfer_pending = false;

typedef struct {
    uint16_t row_start;
    uint16_t row_end;
} st7305_dirty_region_t;

static st7305_dirty_region_t s_dirty_region = {0, 0};

static inline void st7305_send_command(uint8_t command);
static inline void st7305_send_data(uint8_t data);
static void st7305_set_pixel_raw(int x, int y, bool black);

static const uint8_t font8x8[][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00},
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E},
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},
};

static const uint8_t font5x7[][5] = {
    {0x7E,0x09,0x09,0x09,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x40,0x3F,0x00}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x02,0x04,0x08,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x26,0x49,0x49,0x49,0x32}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x30,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03}, {0x61,0x51,0x49,0x45,0x43},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46},
    {0x22,0x41,0x49,0x49,0x36}, {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3E,0x49,0x49,0x49,0x32}, {0x01,0x71,0x09,0x05,0x03}, {0x36,0x49,0x49,0x49,0x36},
    {0x26,0x49,0x49,0x49,0x3E},
    {0x00,0x60,0x60,0x00,0x00}, {0x00,0x36,0x36,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
    {0x00,0x00,0x7F,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
};

static inline bool color_is_black(panel_color_t color)
{
    uint8_t red = (color >> 11) & 0x1F;
    uint8_t green = (color >> 5) & 0x3F;
    uint8_t blue = color & 0x1F;
    uint16_t luma = (red * 299U) + (green * 587U) + (blue * 114U);
    return luma < (32U * 1000U);
}

static void st7305_reset_dirty_region(void)
{
    s_dirty_region.row_start = 0;
    s_dirty_region.row_end = 0;
    s_dirty = false;
}

static void st7305_merge_dirty_region(st7305_dirty_region_t *dst,
                                      const st7305_dirty_region_t *src)
{
    if (src->row_start < dst->row_start) {
        dst->row_start = src->row_start;
    }
    if (src->row_end > dst->row_end) {
        dst->row_end = src->row_end;
    }
}

static void st7305_mark_full_refresh(void)
{
    s_dirty_region = (st7305_dirty_region_t) {
        .row_start = 0,
        .row_end = RLCD_ROW_COUNT - 1,
    };
    s_dirty = true;
}

static void st7305_mark_dirty_blocks(uint16_t row_start, uint16_t row_end,
                                     uint8_t block_start, uint8_t block_end,
                                     bool full_refresh)
{
    (void)block_start;
    (void)block_end;

    if (full_refresh) {
        st7305_mark_full_refresh();
        return;
    }

    if (row_start >= RLCD_ROW_COUNT || block_start >= RLCD_BLOCK_COUNT) {
        return;
    }
    if (row_end >= RLCD_ROW_COUNT) {
        row_end = RLCD_ROW_COUNT - 1;
    }
    if (block_end >= RLCD_BLOCK_COUNT) {
        block_end = RLCD_BLOCK_COUNT - 1;
    }
    if (row_start > row_end || block_start > block_end) {
        return;
    }

    st7305_dirty_region_t region = {
        .row_start = row_start,
        .row_end = row_end,
    };

    if (!s_dirty) {
        s_dirty_region = region;
    } else {
        st7305_merge_dirty_region(&s_dirty_region, &region);
    }

    s_dirty = true;
}

static void st7305_mark_dirty_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    int x0 = x;
    int y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 >= ST7305_LCD_H_RES) {
        x1 = ST7305_LCD_H_RES - 1;
    }
    if (y1 >= ST7305_LCD_V_RES) {
        y1 = ST7305_LCD_V_RES - 1;
    }
    if (x0 > x1 || y0 > y1) {
        return;
    }

    uint16_t row_start = (uint16_t)x0 >> 1;
    uint16_t row_end = (uint16_t)x1 >> 1;
    uint16_t inv_y_start = (uint16_t)(ST7305_LCD_V_RES - 1 - y1);
    uint16_t inv_y_end = (uint16_t)(ST7305_LCD_V_RES - 1 - y0);
    uint8_t block_start = (uint8_t)(inv_y_start >> 2);
    uint8_t block_end = (uint8_t)(inv_y_end >> 2);

    st7305_mark_dirty_blocks(row_start, row_end, block_start, block_end, false);
}

static void st7305_set_window(uint8_t column_start, uint8_t column_end,
                              uint8_t row_start, uint8_t row_end)
{
    st7305_send_command(0x2A);
    st7305_send_data(column_start);
    st7305_send_data(column_end);

    st7305_send_command(0x2B);
    st7305_send_data(row_start);
    st7305_send_data(row_end);
}

static size_t st7305_pack_partial_region(uint16_t row_start, uint16_t row_end)
{
    size_t bytes_per_row = RLCD_BLOCK_COUNT;
    size_t packed_len = 0;

    for (uint16_t row = row_start; row <= row_end; row++) {
        size_t src_offset = (size_t)row * RLCD_BLOCK_COUNT;
        memcpy(s_partial_buffer + packed_len, s_framebuffer + src_offset, bytes_per_row);
        packed_len += bytes_per_row;
    }

    return packed_len;
}

static void st7305_fill_rect_raw(int x, int y, int w, int h, bool black)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            st7305_set_pixel_raw(col, row, black);
        }
    }
}

static inline void st7305_send_command(uint8_t command)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io_handle, command, NULL, 0));
}

static inline void st7305_send_data(uint8_t data)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io_handle, -1, &data, 1));
}

static bool st7305_on_color_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                                          esp_lcd_panel_io_event_data_t *edata,
                                          void *user_ctx)
{
    (void)panel_io;
    (void)edata;

    BaseType_t high_task_wakeup = pdFALSE;
    SemaphoreHandle_t done_sem = (SemaphoreHandle_t)user_ctx;

    s_transfer_pending = false;
    xSemaphoreGiveFromISR(done_sem, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static void st7305_wait_for_transfer_done(void)
{
    if (!s_transfer_pending || !s_transfer_done_sem) {
        return;
    }

    xSemaphoreTake(s_transfer_done_sem, portMAX_DELAY);
}

static void st7305_reset(void)
{
    gpio_set_level(RLCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(RLCD_PIN_RST, 0);
    // Datasheet tRW requires at least a 1 ms low pulse; use 10 ms margin.
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(RLCD_PIN_RST, 1);
    // Datasheet reset-cancel time can be as long as 120 ms in sleep-out mode.
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void st7305_set_pixel_raw(int x, int y, bool black)
{
    if (x < 0 || x >= ST7305_LCD_H_RES || y < 0 || y >= ST7305_LCD_V_RES) {
        return;
    }

    uint16_t inv_y = (uint16_t)(ST7305_LCD_V_RES - 1 - y);
    uint16_t block_y = inv_y >> 2;
    uint8_t local_y = inv_y & 0x03;
    uint16_t byte_x = (uint16_t)x >> 1;
    uint8_t local_x = (uint8_t)x & 0x01;
    uint32_t index = ((uint32_t)byte_x * (ST7305_LCD_V_RES / 4)) + block_y;
    uint8_t bit = 7U - ((local_y << 1) | local_x);
    uint8_t mask = (uint8_t)(1U << bit);

    if (black) {
        s_framebuffer[index] |= mask;
    } else {
        s_framebuffer[index] &= (uint8_t)~mask;
    }
}

static void st7305_draw_outline_rect(int x, int y, int w, int h, bool black)
{
    for (int col = x; col < x + w; col++) {
        st7305_set_pixel_raw(col, y, black);
        st7305_set_pixel_raw(col, y + h - 1, black);
    }
    for (int row = y; row < y + h; row++) {
        st7305_set_pixel_raw(x, row, black);
        st7305_set_pixel_raw(x + w - 1, row, black);
    }
}

static void st7305_draw_char_small_internal(int x, int y, char c, bool fg_black, bool bg_black)
{
    int glyph_idx = -1;

    if (c >= 'A' && c <= 'Z') {
        glyph_idx = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
        glyph_idx = c - 'a';
    } else if (c >= '0' && c <= '9') {
        glyph_idx = 26 + (c - '0');
    } else if (c == '.') {
        glyph_idx = 36;
    } else if (c == ':') {
        glyph_idx = 37;
    } else if (c == '-') {
        glyph_idx = 38;
    } else if (c == '|') {
        glyph_idx = 39;
    } else if (c == '/') {
        glyph_idx = 40;
    } else if (c == ' ') {
        return;
    }

    if (glyph_idx < 0) {
        return;
    }

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 6; col++) {
            bool black = bg_black;
            if (col < 5) {
                uint8_t column_data = font5x7[glyph_idx][col];
                black = (column_data & (1 << row)) ? fg_black : bg_black;
            }
            st7305_set_pixel_raw(x + col, y + row, black);
        }
    }
}

esp_err_t st7305_rlcd_init(void)
{
    if (s_io_handle && s_framebuffer) {
        return ESP_OK;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = RLCD_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = RLCD_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = RLCD_BUF_LEN,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(RLCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = RLCD_PIN_DC,
        .cs_gpio_num = RLCD_PIN_CS,
        .pclk_hz = RLCD_SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)RLCD_SPI_HOST, &io_config, &s_io_handle));

    if (!s_transfer_done_sem) {
        s_transfer_done_sem = xSemaphoreCreateBinary();
        if (!s_transfer_done_sem) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = st7305_on_color_transfer_done,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(s_io_handle, &callbacks, s_transfer_done_sem));

    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << RLCD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));

    s_framebuffer = heap_caps_malloc(RLCD_BUF_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_framebuffer) {
        s_framebuffer = heap_caps_malloc(RLCD_BUF_LEN, MALLOC_CAP_8BIT);
    }
    if (!s_framebuffer) {
        return ESP_ERR_NO_MEM;
    }

    if (!s_partial_buffer) {
        s_partial_buffer = heap_caps_malloc(RLCD_BUF_LEN, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!s_partial_buffer) {
            s_partial_buffer = heap_caps_malloc(RLCD_BUF_LEN, MALLOC_CAP_8BIT);
        }
        if (!s_partial_buffer) {
            return ESP_ERR_NO_MEM;
        }
    }

    st7305_reset_dirty_region();

    st7305_reset();

    // Apply a software reset as well, then honor the datasheet's 5 ms quiet time.
    st7305_send_command(0x01);
    vTaskDelay(pdMS_TO_TICKS(10));

    st7305_send_command(0xD6);
    st7305_send_data(0x17);
    st7305_send_data(0x02);

    st7305_send_command(0xD1);
    st7305_send_data(0x01);

    st7305_send_command(0xC0);
    st7305_send_data(0x11);
    st7305_send_data(0x04);

    st7305_send_command(0xC1);
    for (int i = 0; i < 4; i++) st7305_send_data(0x69);

    st7305_send_command(0xC2);
    for (int i = 0; i < 4; i++) st7305_send_data(0x19);

    st7305_send_command(0xC4);
    for (int i = 0; i < 4; i++) st7305_send_data(0x4B);

    st7305_send_command(0xC5);
    for (int i = 0; i < 4; i++) st7305_send_data(0x19);

    st7305_send_command(0xD8);
    st7305_send_data(0x80);
    st7305_send_data(0xE9);

    st7305_send_command(0xB2);
    st7305_send_data(0x02);

    st7305_send_command(0xB3);
    st7305_send_data(0xE5);
    st7305_send_data(0xF6);
    st7305_send_data(0x05);
    st7305_send_data(0x46);
    st7305_send_data(0x77);
    st7305_send_data(0x77);
    st7305_send_data(0x77);
    st7305_send_data(0x76);
    st7305_send_data(0x45);

    st7305_send_command(0xB4);
    st7305_send_data(0x05);
    st7305_send_data(0x46);
    st7305_send_data(0x77);
    st7305_send_data(0x77);
    st7305_send_data(0x77);
    st7305_send_data(0x77);
    st7305_send_data(0x76);
    st7305_send_data(0x45);

    st7305_send_command(0x62);
    st7305_send_data(0x32);
    st7305_send_data(0x03);
    st7305_send_data(0x1F);

    st7305_send_command(0xB7);
    st7305_send_data(0x13);

    // Keep the display in the higher drive-strength mode used by the vendor init.
    st7305_send_command(0xB0);
    st7305_send_data(0x64);

    // Max refresh-rate mode per ST7305 frame-rate control guidance.
    // Mode 0 targets the controller's highest refresh rate, roughly 51 Hz.
    st7305_send_command(0xB1);
    st7305_send_data(0x00);

    st7305_send_command(0x11);
    vTaskDelay(pdMS_TO_TICKS(200));

    st7305_send_command(0xC9);
    st7305_send_data(0x00);

    st7305_send_command(0x36);
    st7305_send_data(0x48);

    st7305_send_command(0x3A);
    st7305_send_data(0x11);

    st7305_send_command(0xB9);
    st7305_send_data(0x20);

    st7305_send_command(0xB8);
    st7305_send_data(0x29);

    st7305_send_command(0x21);

    st7305_send_command(0x2A);
    st7305_send_data(0x12);
    st7305_send_data(0x2A);

    st7305_send_command(0x2B);
    st7305_send_data(0x00);
    st7305_send_data(0xC7);

    st7305_send_command(0x35);
    st7305_send_data(0x00);

    st7305_send_command(0xD0);
    st7305_send_data(0xFF);

    // Keep the panel in high power mode for the higher refresh rate.
    st7305_send_command(0x38);
    st7305_send_command(0x29);

    st7305_rlcd_fill_screen(PANEL_COLOR_WHITE);
    st7305_rlcd_present();
    ESP_LOGI(TAG, "ST7305 RLCD initialized in landscape 400x300 mode");
    return ESP_OK;
}

void st7305_rlcd_fill_screen(panel_color_t color)
{
    st7305_wait_for_transfer_done();
    memset(s_framebuffer, color_is_black(color) ? 0xFF : 0x00, RLCD_BUF_LEN);
    st7305_mark_dirty_blocks(0, RLCD_ROW_COUNT - 1, 0, RLCD_BLOCK_COUNT - 1, true);
}

void st7305_rlcd_fill_rect(int x, int y, int w, int h, panel_color_t color)
{
    st7305_wait_for_transfer_done();
    if (w <= 0 || h <= 0) {
        return;
    }

    st7305_fill_rect_raw(x, y, w, h, color_is_black(color));
    st7305_mark_dirty_rect(x, y, w, h);
}

void st7305_rlcd_draw_pixel(int x, int y, panel_color_t color)
{
    st7305_wait_for_transfer_done();
    st7305_set_pixel_raw(x, y, color_is_black(color));
    st7305_mark_dirty_rect(x, y, 1, 1);
}

void st7305_rlcd_draw_string(int x, int y, const char *str, panel_color_t fg_color, panel_color_t bg_color, int scale)
{
    st7305_wait_for_transfer_done();
    if (!str || scale <= 0) {
        return;
    }

    size_t text_len = strlen(str);

    bool fg_black = color_is_black(fg_color);
    bool bg_black = color_is_black(bg_color);

    while (*str) {
        char c = *str;
        if (c < 32 || c > 126) {
            c = '?';
        }
        int idx = c - 32;
        for (int row = 0; row < 8; row++) {
            uint8_t line = font8x8[idx][row];
            for (int col = 0; col < 8; col++) {
                bool pixel_black = (line & (1 << col)) ? fg_black : bg_black;
                int base_x = x + (col * scale);
                int base_y = y + (row * scale);
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        st7305_set_pixel_raw(base_x + sx, base_y + sy, pixel_black);
                    }
                }
            }
        }
        x += 8 * scale;
        str++;
    }

    st7305_mark_dirty_rect(x - (int)(text_len * 8U * (unsigned)scale), y,
                           (int)(text_len * 8U * (unsigned)scale), 8 * scale);
}

void st7305_rlcd_draw_string_small(int x, int y, const char *str, panel_color_t fg_color, panel_color_t bg_color)
{
    st7305_wait_for_transfer_done();
    if (!str) {
        return;
    }

    size_t text_len = strlen(str);

    bool fg_black = color_is_black(fg_color);
    bool bg_black = color_is_black(bg_color);

    while (*str) {
        if (*str != ' ') {
            st7305_draw_char_small_internal(x, y, *str, fg_black, bg_black);
        }
        x += 6;
        str++;
    }

    st7305_mark_dirty_rect(x - (int)(text_len * 6U), y, (int)(text_len * 6U), 7);
}

void st7305_rlcd_draw_led_span(uint32_t bits, int num_leds, int x_start, int y,
                               int led_size, int spacing, panel_color_t on_color,
                               panel_color_t off_color, panel_color_t bg_color,
                               int left_index, int right_index)
{
    st7305_wait_for_transfer_done();
    (void)off_color;
    if (num_leds <= 0 || left_index < right_index) {
        return;
    }

    bool on_black = color_is_black(on_color);
    bool bg_black = color_is_black(bg_color);

    int left_led_x = x_start + (num_leds - 1 - left_index) * spacing;
    int right_led_x = x_start + (num_leds - 1 - right_index) * spacing;

    for (int led = left_index; led >= right_index; led--) {
        int led_x = x_start + (num_leds - 1 - led) * spacing;
        bool led_on = ((bits >> led) & 1U) != 0;
        st7305_fill_rect_raw(led_x, y, led_size, led_size, bg_black);
        if (led_on) {
            st7305_fill_rect_raw(led_x, y, led_size, led_size, on_black);
        } else {
            st7305_draw_outline_rect(led_x, y, led_size, led_size, !bg_black);
        }
    }

    st7305_mark_dirty_rect(left_led_x, y, (right_led_x - left_led_x) + led_size, led_size);
}

void st7305_rlcd_draw_led_row(uint32_t bits, int num_leds, int x_start, int y,
                              int led_size, int spacing, panel_color_t on_color,
                              panel_color_t off_color, panel_color_t bg_color)
{
    st7305_rlcd_draw_led_span(bits, num_leds, x_start, y, led_size, spacing,
                              on_color, off_color, bg_color, num_leds - 1, 0);
}

void st7305_rlcd_present(void)
{
    if (!s_dirty || !s_framebuffer) {
        return;
    }

    if (!s_partial_buffer) {
        ESP_LOGE(TAG, "Partial buffer unavailable; skipping present");
        st7305_reset_dirty_region();
        return;
    }

    size_t tx_len = st7305_pack_partial_region(s_dirty_region.row_start, s_dirty_region.row_end);

    st7305_set_window(RLCD_COLUMN_ADDR_START,
              RLCD_COLUMN_ADDR_END,
                      (uint8_t)s_dirty_region.row_start,
                      (uint8_t)s_dirty_region.row_end);

    st7305_send_command(0x2C);
    s_transfer_pending = true;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io_handle, -1, s_partial_buffer, tx_len));
    st7305_wait_for_transfer_done();

    st7305_reset_dirty_region();
}

void st7305_rlcd_set_backlight(int brightness)
{
    (void)brightness;
}

bool st7305_rlcd_is_dirty(void)
{
    return s_dirty;
}