/* Raspberry Pi Sense HAT front panel integration for the local Altair runner.
 *
 * Real implementation (Linux + ALTAIR_LOCAL_ENABLE_SENSE_HAT) ported from the
 * Altair-8800-Emulator reference project's front_panel_pi_sense_hat.c, with the
 * bit-reversal the reference performs in its panel thread folded in. A
 * dedicated background thread samples the CPU state at a fixed ~50 Hz using
 * clock_nanosleep(TIMER_ABSTIME) so the panel cadence is decoupled from the
 * emulator's instruction loop and does not drift. Stub implementation
 * otherwise.
 */

#include "sense_hat_panel.h"

#if defined(ALTAIR_LOCAL_ENABLE_SENSE_HAT) && defined(__linux__)

#include "graphics.h"
#include "led_panel.h"
#include "sense_hat.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The Sense HAT 8x8 RGB matrix sits on the same I2C bus as the sensors;
   the kernel exposes the matrix as an RGB565 framebuffer. */
#define NUM_OF_LEDS 64

/* Fixed sampler period: 50 Hz => 20 ms. */
#define PANEL_SAMPLE_PERIOD_NS (20 * 1000000L)

/* Default I2C bus the Sense HAT sensors are wired to (/dev/i2c-1). */
#define SENSE_HAT_I2C_BUS 1

/* Colors for status (red), data (blue) and bus (green) in RGB565. */
#define COLOR_RED   0x5000
#define COLOR_BLUE  0x002C
#define COLOR_GREEN 0x0180

/* panel_mode: 0 = bus data, 1 = font, 2 = bitmap. */
#define PANEL_BUS_MODE    0
#define PANEL_FONT_MODE   1
#define PANEL_BITMAP_MODE 2

/* Bit-reverse a nibble; used to flip the bus/data/status bits to match the
   physical LED ordering (mirrors reverse_lut in the reference main.c). */
static const uint8_t reverse_lut[16] = {
    0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe, 0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf};

typedef union
{
    uint8_t bitmap[8];
    uint64_t bitmap64;
} PIXEL_MAP;

/* active / panel_mode are shared between the sampler thread and the CPU
   (I/O) thread. They are plain scalars accessed atomically (relaxed) so reads
   are not torn and writes become visible without taking a lock. */
static atomic_bool active;
static atomic_int panel_mode;

/* Sampler thread + run flag. */
static pthread_t sampler_thread;
static atomic_bool sampler_run;
static bool sampler_started;

/* How the sampler reads CPU state (provided by main.c). */
static sense_hat_sample_fn sample_cpu;

static uint16_t panel_buffer[NUM_OF_LEDS];
static uint16_t panel_8x8_buffer[NUM_OF_LEDS];
static uint8_t bitmap_rows[8];
static PIXEL_MAP pixel_map;

/* Change detection for the bus-mode paint. Owned solely by the sampler
   thread, so it needs no synchronisation. */
static uint8_t last_status;
static uint8_t last_data;
static uint16_t last_bus;
static bool have_last;

static void uint8_to_uint16_t(uint8_t bitmap, uint16_t *buffer, uint16_t color)
{
    uint16_t mask        = 1;
    uint8_t pixel_number = 0;

    while (pixel_number < 8)
    {
        buffer[pixel_number++] = (bitmap & mask) ? color : 0x0000;
        mask                   = (uint16_t)(mask << 1);
    }
}

// Rotate the panel buffer 180 degrees.
static void rotate_panel_180(uint16_t *buffer)
{
    uint16_t temp;
    for (int row = 0; row < 4; row++)
    {
        int opposite_row = 7 - row;
        for (int col = 0; col < 8; col++)
        {
            int opposite_col = 7 - col;
            temp             = buffer[row * 8 + col];
            buffer[row * 8 + col]                   = buffer[opposite_row * 8 + opposite_col];
            buffer[opposite_row * 8 + opposite_col] = temp;
        }
    }
}

static void paint_bus_state(uint8_t status, uint8_t data, uint16_t bus)
{
    // Status in red (row 0)
    uint8_to_uint16_t(status, panel_buffer, COLOR_RED);
    // Clear rows 1-2
    memset(panel_buffer + (1 * 8), 0, 2 * 8 * sizeof(uint16_t));
    // Data in blue (row 3)
    uint8_to_uint16_t(data, panel_buffer + (3 * 8), COLOR_BLUE);
    // Clear rows 4-5
    memset(panel_buffer + (4 * 8), 0, 2 * 8 * sizeof(uint16_t));
    // Bus high byte in green (row 6)
    uint8_to_uint16_t((uint8_t)(bus >> 8), panel_buffer + (6 * 8), COLOR_GREEN);
    // Bus low byte in green (row 7)
    uint8_to_uint16_t((uint8_t)(bus), panel_buffer + (7 * 8), COLOR_GREEN);

    rotate_panel_180(panel_buffer);
    pi_sense_8x8_panel_update(panel_buffer, PI_SENSE_8x8_BUFFER_SIZE);
}

/* Sample the live CPU state and paint the bus display. Runs on the sampler
   thread; only paints in bus mode and only when the (bit-reversed) state has
   actually changed. */
static void sample_and_paint(void)
{
    if (atomic_load_explicit(&panel_mode, memory_order_relaxed) != PANEL_BUS_MODE)
    {
        return;
    }

    uint8_t status = 0;
    uint8_t data   = 0;
    uint16_t bus   = 0;
    sample_cpu(&status, &data, &bus);

    // Flip the bits to match the physical LED ordering.
    uint8_t r_status = (uint8_t)(reverse_lut[(status & 0xf0) >> 4] | reverse_lut[status & 0xf] << 4);
    uint8_t r_data   = (uint8_t)(reverse_lut[(data & 0xf0) >> 4] | reverse_lut[data & 0xf] << 4);
    uint16_t r_bus   = (uint16_t)(reverse_lut[(bus & 0xf000) >> 12] << 8 | reverse_lut[(bus & 0x0f00) >> 8] << 12 |
                                reverse_lut[(bus & 0xf0) >> 4] | reverse_lut[bus & 0xf] << 4);

    if (have_last && r_status == last_status && r_data == last_data && r_bus == last_bus)
    {
        return;
    }

    last_status = r_status;
    last_data   = r_data;
    last_bus    = r_bus;
    have_last   = true;

    paint_bus_state(r_status, r_data, r_bus);
}

/* Fixed-rate sampler loop: wake every 10 ms on an absolute monotonic deadline
   so timing does not drift with paint latency. */
static void *sampler_main(void *arg)
{
    (void)arg;

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    while (atomic_load_explicit(&sampler_run, memory_order_relaxed))
    {
        next.tv_nsec += PANEL_SAMPLE_PERIOD_NS;
        while (next.tv_nsec >= 1000000000L)
        {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

        if (!atomic_load_explicit(&sampler_run, memory_order_relaxed))
        {
            break;
        }

        sample_and_paint();
    }

    return NULL;
}

bool sense_hat_panel_enable(sense_hat_sample_fn sampler)
{
    if (atomic_load_explicit(&active, memory_order_relaxed))
    {
        return true;
    }

    if (sampler == NULL)
    {
        return false;
    }

    fprintf(stderr, "[sense-hat] Bringing up Raspberry Pi Sense HAT front panel...\n");

    /* The 8x8 LED matrix IS the front panel, so it is required. The
       environmental sensors are a bonus (ports 63) and must never block the
       panel from coming up, e.g. when the sensor I2C bus number differs (the
       Pi 5 moved buses around). */
    if (!pi_sense_hat_init())
    {
        fprintf(stderr,
                "[sense-hat] ERROR: could not open the LED matrix framebuffer "
                "(no /dev/fb* with id 'RPi-Sense FB').\n"
                "[sense-hat]   - Is the Sense HAT seated and the rpisense-fb "
                "kernel module loaded? (lsmod | grep rpisense)\n"
                "[sense-hat]   - Did you start the container with --privileged "
                "so it can see /dev/fb*?\n"
                "[sense-hat]   - You can force a device with "
                "-e ALTAIR_SENSE_HAT_FB=1 (or a full /dev/fbN path).\n");
        return false;
    }
    fprintf(stderr, "[sense-hat] LED matrix framebuffer ready.\n");

    int i2c_bus = SENSE_HAT_I2C_BUS;
    const char *i2c_override = getenv("ALTAIR_SENSE_HAT_I2C");
    if (i2c_override != NULL && i2c_override[0] != '\0')
    {
        i2c_bus = atoi(i2c_override);
    }

    fprintf(stderr, "[sense-hat] Probing environmental sensors on /dev/i2c-%d...\n", i2c_bus);
    if (pi_sense_hat_sensors_init(i2c_bus))
    {
        fprintf(stderr, "[sense-hat] Environmental sensors (HTS221/LPS25H) ready.\n");
    }
    else
    {
        fprintf(stderr,
                "[sense-hat] WARNING: environmental sensors unavailable; "
                "front-panel LEDs will still work, sensor ports (63) read 0.\n"
                "[sense-hat]   - Pass the I2C bus with --device=/dev/i2c-1 "
                "(override the bus number with -e ALTAIR_SENSE_HAT_I2C=N).\n");
        pi_sense_hat_sensors_close();
    }

    sample_cpu = sampler;
    atomic_store_explicit(&panel_mode, PANEL_BUS_MODE, memory_order_relaxed);
    have_last          = false;
    pixel_map.bitmap64 = 0;
    atomic_store_explicit(&active, true, memory_order_relaxed);

    atomic_store_explicit(&sampler_run, true, memory_order_relaxed);
    if (pthread_create(&sampler_thread, NULL, sampler_main, NULL) != 0)
    {
        fprintf(stderr, "[sense-hat] ERROR: failed to start the 50 Hz sampler thread.\n");
        atomic_store_explicit(&sampler_run, false, memory_order_relaxed);
        atomic_store_explicit(&active, false, memory_order_relaxed);
        pi_sense_hat_sensors_close();
        pi_sense_hat_close();
        return false;
    }
    sampler_started = true;

    fprintf(stderr, "[sense-hat] Front panel active: sampling CPU state at 50 Hz.\n");
    return true;
}

void sense_hat_panel_shutdown(void)
{
    if (!atomic_load_explicit(&active, memory_order_relaxed))
    {
        return;
    }

    atomic_store_explicit(&sampler_run, false, memory_order_relaxed);
    if (sampler_started)
    {
        pthread_join(sampler_thread, NULL);
        sampler_started = false;
    }

    atomic_store_explicit(&active, false, memory_order_relaxed);

    pi_sense_hat_sensors_close();
    pi_sense_hat_close();
    memset(panel_buffer, 0, sizeof(panel_buffer));
    memset(panel_8x8_buffer, 0, sizeof(panel_8x8_buffer));
    pixel_map.bitmap64 = 0;
}

bool sense_hat_panel_is_active(void)
{
    return atomic_load_explicit(&active, memory_order_relaxed);
}

static void panel_draw_bitmap(void)
{
    memset(panel_8x8_buffer, 0x00, sizeof(panel_8x8_buffer));
    gfx_rotate_counterclockwise(pixel_map.bitmap, 1, 1, bitmap_rows);
    gfx_reverse_panel(bitmap_rows);
    gfx_rotate_counterclockwise(bitmap_rows, 1, 1, bitmap_rows);
    gfx_bitmap_to_rgb(bitmap_rows, panel_8x8_buffer, sizeof(panel_8x8_buffer));
    rotate_panel_180(panel_8x8_buffer);
    pi_sense_8x8_panel_update(panel_8x8_buffer, sizeof(panel_8x8_buffer));
}

static bool sense_hat_handle_led_output(int port, uint8_t data)
{
    if (!atomic_load_explicit(&active, memory_order_relaxed))
    {
        return false;
    }

    switch (port)
    {
        case 65: // Set 8x8 LED panel color
            if (data != 0 && data < 3)
            {
                data = 3;
            }
            if (data > 15)
            {
                data = 15;
            }
            // color tracked by graphics layer; nothing else required here.
            return true;
        case 80: // panel_mode 0 = bus data, 1 = font, 2 = bitmap
            if (data < 3)
            {
                atomic_store_explicit(&panel_mode, data, memory_order_relaxed);
                have_last = false; // force a repaint when returning to bus mode
            }
            return true;
        case 81: // set font color
            gfx_set_color(data);
            return true;
        case 85: // display character
            memset(panel_8x8_buffer, 0x00, sizeof(panel_8x8_buffer));
            gfx_load_character(data, bitmap_rows);
            gfx_rotate_counterclockwise(bitmap_rows, 1, 1, bitmap_rows);
            gfx_reverse_panel(bitmap_rows);
            gfx_rotate_counterclockwise(bitmap_rows, 1, 1, bitmap_rows);
            gfx_bitmap_to_rgb(bitmap_rows, panel_8x8_buffer, sizeof(panel_8x8_buffer));
            rotate_panel_180(panel_8x8_buffer);
            pi_sense_8x8_panel_update(panel_8x8_buffer, sizeof(panel_8x8_buffer));
            return true;
        case 90:
        case 91:
        case 92:
        case 93:
        case 94:
        case 95:
        case 96:
        case 97:
            pixel_map.bitmap[port - 90] = data;
            return true;
        case 98: // pixel on
            if (data < 64)
            {
                pixel_map.bitmap64 |= (uint64_t)1 << data;
            }
            return true;
        case 99: // pixel off
            if (data < 64)
            {
                pixel_map.bitmap64 &= ~((uint64_t)1 << data);
            }
            return true;
        case 100: // pixel flip
            if (data < 64)
            {
                pixel_map.bitmap64 ^= (uint64_t)1 << data;
            }
            return true;
        case 101: // clear all pixels
            pixel_map.bitmap64 = 0;
            return true;
        case 102: // bitmap draw
            panel_draw_bitmap();
            return true;
        default:
            return false;
    }
}

static size_t sense_hat_read_sensor(uint8_t data, char *buffer, size_t buffer_length)
{
    if (!atomic_load_explicit(&active, memory_order_relaxed))
    {
        return 0;
    }

    int len = 0;
    switch (data)
    {
        case 0: // temperature minus 1 for very rough calibration
            len = snprintf(buffer, buffer_length, "%d", (int)get_temperature_from_lps25h() - 1);
            break;
        case 1: // pressure
            len = snprintf(buffer, buffer_length, "%d", get_pressure());
            break;
        case 2: // light (not available on the Sense HAT)
            len = snprintf(buffer, buffer_length, "%d", 0);
            break;
        case 3: // humidity
            len = snprintf(buffer, buffer_length, "%d", (int)get_humidity());
            break;
        default:
            break;
    }

    return len < 0 ? 0 : (size_t)len;
}

size_t sense_hat_panel_output(uint8_t port, uint8_t data, char *buffer, size_t buffer_length)
{
    if (port == 63)
    {
        return sense_hat_read_sensor(data, buffer, buffer_length);
    }

    sense_hat_handle_led_output(port, data);
    return 0;
}

#else /* Stub implementation: Sense HAT not compiled in. */

bool sense_hat_panel_enable(sense_hat_sample_fn sampler)
{
    (void)sampler;
    return false;
}

void sense_hat_panel_shutdown(void)
{
}

bool sense_hat_panel_is_active(void)
{
    return false;
}

size_t sense_hat_panel_output(uint8_t port, uint8_t data, char *buffer, size_t buffer_length)
{
    (void)port;
    (void)data;
    (void)buffer;
    (void)buffer_length;
    return 0;
}

#endif
