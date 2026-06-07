/* Raspberry Pi Sense HAT front panel integration for the local Altair runner.
 *
 * This is the runtime glue between the emulator and the ported Sense HAT
 * drivers (led_panel / graphics / sense_hat sensors). The Sense HAT is a
 * Linux-only peripheral (Linux framebuffer + I2C), so the real implementation
 * is compiled only for the Docker/Linux build, which is built with
 * -DALTAIR_LOCAL_ENABLE_SENSE_HAT=ON. On every other build (macOS, Windows,
 * plain desktop Linux) the functions below are inert stubs so main.c and
 * io_ports.c can call them unconditionally.
 *
 * Even when compiled in, the panel must be opted into at run time
 * (--sense-hat or ALTAIR_SENSE_HAT=1) and the hardware must be present;
 * otherwise sense_hat_panel_enable() returns false and everything stays inert.
 *
 * When active, a dedicated background thread samples the CPU status/data/
 * address state at a fixed ~50 Hz (independent of emulator speed) and paints
 * the front-panel LEDs. Sampling three scalar CPU fields is an inherent
 * snapshot, so no lock is taken; a momentarily mixed sample simply corrects
 * itself on the next 10 ms tick.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Reads the current CPU status / data bus / address bus. Supplied by the
 * caller so this module stays decoupled from the emulator core. Called from
 * the sampler thread at ~50 Hz. */
typedef void (*sense_hat_sample_fn)(uint8_t *status, uint8_t *data, uint16_t *bus);

/* Probe for the Sense HAT, bring up the LED panel + sensors, and start the
 * 50 Hz sampler thread that mirrors CPU state via `sampler`.
 * Returns true only when the hardware was found and initialised. Safe to call
 * when support is not compiled in (always returns false). */
bool sense_hat_panel_enable(sense_hat_sample_fn sampler);

/* Stop the sampler thread and release the LED panel + sensors. Safe to call
 * when inactive. */
void sense_hat_panel_shutdown(void);

/* True when the panel is compiled in, enabled and successfully initialised. */
bool sense_hat_panel_is_active(void);

/* Handle an OUT to any Sense HAT port, dispatched from io_ports.c exactly like
 * the other port drivers (time_output, weather_output, ...). Port 63 reads an
 * onboard sensor (the selector in `data` chooses temperature/pressure/light/
 * humidity), writes the formatted reading to `buffer`, and returns the byte
 * count (read back via port 200). Ports 65, 80, 81, 85 and 90-102 drive the
 * 8x8 LED matrix and return 0. Inert (returns 0) when the panel is not active. */
size_t sense_hat_panel_output(uint8_t port, uint8_t data, char *buffer, size_t buffer_length);
